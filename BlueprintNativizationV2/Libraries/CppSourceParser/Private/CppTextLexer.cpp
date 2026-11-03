/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "CppTextLexer.h"
#include <cctype>
#include <filesystem>
#include <stdexcept>

std::filesystem::path MakeCppSourcePath(const std::string &Text)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(Text.begin(), Text.end()));
#else
    return std::filesystem::u8path(Text);
#endif
}

std::string NormalizeCppPath(const std::string &Path, const std::string &Directory)
{
    if (Path.empty())
    {
        return {};
    }
    auto Result = MakeCppSourcePath(Path);
    if (Result.is_relative())
    {
        Result = MakeCppSourcePath(Directory) / Result;
    }
    const auto Utf8 = std::filesystem::absolute(Result).lexically_normal().generic_u8string();
    return std::string(Utf8.begin(), Utf8.end());
}

std::vector<FCppTextToken> TokenizeCppText(const std::string &File, const std::string &Text,
                                           std::vector<FCppDiagnostic> &Diagnostics)
{
    std::vector<FCppTextToken> Result;
    size_t Position = 0;
    if (Text.compare(0, 3, "\xef\xbb\xbf") == 0)
    {
        Position = 3;
    }
    unsigned Line = 1;
    unsigned Column = 1;
    bool bLineStart = true;
    auto Advance = [&](size_t End)
    {
        while (Position < End)
        {
            if (Text[Position] == '\n')
            {
                ++Line;
                Column = 1;
                bLineStart = true;
            }
            else
            {
                ++Column;
            }
            ++Position;
        }
    };
    while (Position < Text.size())
    {
        if (std::isspace(static_cast<unsigned char>(Text[Position])))
        {
            Advance(Position + 1);
            continue;
        }
        if (Text.compare(Position, 2, "//") == 0)
        {
            size_t End = Text.find('\n', Position);
            if (End == std::string::npos)
            {
                End = Text.size();
            }
            Advance(End);
            continue;
        }
        if (Text.compare(Position, 2, "/*") == 0)
        {
            size_t End = Text.find("*/", Position + 2);
            if (End == std::string::npos)
            {
                Diagnostics.push_back({ECppDiagnosticSeverity::Error,
                                       "Unterminated block comment.",
                                       {File, Line, Column, static_cast<unsigned>(Position)}});
                break;
            }
            Advance(End + 2);
            continue;
        }
        const size_t Start = Position;
        FCppTextToken Token;
        Token.Range.Begin = {File, Line, Column, static_cast<unsigned>(Start)};
        Token.Kind = "Punctuation";
        size_t End = Start + 1;
        if (Text[Start] == '#' && bLineStart)
        {
            Token.Kind = "Directive";
            while (End < Text.size())
            {
                if (Text[End] == '\n')
                {
                    size_t Previous = End;
                    if (Previous > Start && Text[Previous - 1] == '\r')
                    {
                        --Previous;
                    }
                    if (Previous == Start || Text[Previous - 1] != '\\')
                    {
                        break;
                    }
                }
                ++End;
            }
        }
        else
        {
            size_t Quote = Start;
            bool bRaw = false;
            for (const std::string Prefix :
                 {"u8R\"", "LR\"", "uR\"", "UR\"", "R\"", "u8\"", "L\"", "u\"", "U\"", "L'", "u'", "U'"})
            {
                if (Text.compare(Start, Prefix.size(), Prefix) == 0)
                {
                    Quote = Start + Prefix.size() - 1;
                    bRaw = Prefix.find('R') != std::string::npos;
                    break;
                }
            }
            if (Text[Quote] == '"' || Text[Quote] == '\'')
            {
                Token.Kind = "Literal";
                bool bClosed = false;
                if (bRaw)
                {
                    const size_t Open = Text.find('(', Quote + 1);
                    if (Open != std::string::npos && Open - Quote <= 17)
                    {
                        const std::string Closing = ")" + Text.substr(Quote + 1, Open - Quote - 1) + "\"";
                        const size_t Close = Text.find(Closing, Open + 1);
                        if (Close != std::string::npos)
                        {
                            End = Close + Closing.size();
                            bClosed = true;
                        }
                    }
                }
                else
                {
                    End = Quote + 1;
                    while (End < Text.size())
                    {
                        if (Text[End] == '\\' && End + 1 < Text.size())
                        {
                            End += 2;
                            continue;
                        }
                        if (Text[End++] == Text[Quote])
                        {
                            bClosed = true;
                            break;
                        }
                    }
                }
                if (!bClosed)
                {
                    Diagnostics.push_back({ECppDiagnosticSeverity::Error, "Unterminated string or character literal.",
                                           Token.Range.Begin});
                    End = Text.size();
                }
            }
            else if (std::isalpha(static_cast<unsigned char>(Text[Start])) || Text[Start] == '_' ||
                     static_cast<unsigned char>(Text[Start]) >= 128)
            {
                Token.Kind = "Identifier";
                while (End < Text.size() && (std::isalnum(static_cast<unsigned char>(Text[End])) || Text[End] == '_' ||
                                             static_cast<unsigned char>(Text[End]) >= 128))
                {
                    ++End;
                }
            }
            else if (std::isdigit(static_cast<unsigned char>(Text[Start])))
            {
                Token.Kind = "Literal";
                while (End < Text.size() && (std::isalnum(static_cast<unsigned char>(Text[End])) || Text[End] == '.' ||
                                             Text[End] == '\'' || Text[End] == '_'))
                {
                    ++End;
                }
            }
            else
            {
                for (const std::string Operator : {"...", "::", "->", "&&", "||", "==", "!=", "<=", ">=", "++", "--",
                                                   "+=", "-=", "*=", "/=", "&=", "|="})
                {
                    if (Text.compare(Start, Operator.size(), Operator) == 0)
                    {
                        End = Start + Operator.size();
                        break;
                    }
                }
            }
        }
        Token.Text = Text.substr(Start, End - Start);
        Advance(End);
        Token.Range.End = {File, Line, Column, static_cast<unsigned>(End)};
        Result.push_back(std::move(Token));
        bLineStart = false;
    }
    return Result;
}

std::vector<std::string> SplitCppCommand(const std::string &Text)
{
    std::vector<std::string> Result;
    std::string Argument;
    char Quote = 0;
    bool bStarted = false;
    for (size_t Position = 0; Position < Text.size(); ++Position)
    {
        const char Character = Text[Position];
        if (Character == '\\')
        {
            size_t End = Position;
            while (End < Text.size() && Text[End] == '\\')
            {
                ++End;
            }
            const size_t Count = End - Position;
            if (End < Text.size() && Text[End] == '"')
            {
                Argument.append(Count / 2, '\\');
                Position = End;
                if (Count % 2 != 0)
                {
                    Argument += '"';
                }
                else if (Quote == '"')
                {
                    Quote = 0;
                }
                else
                {
                    Quote = '"';
                }
            }
            else
            {
                Argument.append(Count, '\\');
                Position = End - 1;
            }
            bStarted = true;
        }
        else if (Character == '"' || Character == '\'')
        {
            if (Quote == Character)
            {
                Quote = 0;
            }
            else if (Quote == 0)
            {
                Quote = Character;
            }
            else
            {
                Argument += Character;
            }
            bStarted = true;
        }
        else if (Quote == 0 && std::isspace(static_cast<unsigned char>(Character)))
        {
            if (bStarted)
            {
                Result.push_back(Argument);
                Argument.clear();
                bStarted = false;
            }
        }
        else
        {
            Argument += Character;
            bStarted = true;
        }
    }
    if (Quote != 0)
    {
        throw std::runtime_error("Unterminated quote in compiler command.");
    }
    if (bStarted)
    {
        Result.push_back(Argument);
    }
    return Result;
}
