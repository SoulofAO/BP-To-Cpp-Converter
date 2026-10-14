#include "CppTextLexer.h"
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

struct FCompilationJsonValue
{
    char Kind = 0;
    std::string Text;
    std::vector<FCompilationJsonValue> Elements;
    std::unordered_map<std::string, FCompilationJsonValue> Properties;
};

class FCompilationJsonReader
{
  public:
    const std::string &Text;
    size_t Position = 0;
    explicit FCompilationJsonReader(const std::string &Source) : Text(Source) {}

    void SkipSpace()
    {
        while (Position < Text.size() && std::isspace(static_cast<unsigned char>(Text[Position])))
        {
            ++Position;
        }
    }

    unsigned ReadHex()
    {
        unsigned Result = 0;
        for (int Digit = 0; Digit < 4; ++Digit)
        {
            if (Position >= Text.size() || !std::isxdigit(static_cast<unsigned char>(Text[Position])))
            {
                throw std::runtime_error("Invalid JSON Unicode escape.");
            }
            const char Character = Text[Position++];
            Result *= 16;
            if (Character >= '0' && Character <= '9')
            {
                Result += Character - '0';
            }
            else
            {
                Result += std::tolower(static_cast<unsigned char>(Character)) - 'a' + 10;
            }
        }
        return Result;
    }

    std::string ReadString()
    {
        if (Position >= Text.size() || Text[Position++] != '"')
        {
            throw std::runtime_error("Expected JSON string.");
        }
        std::string Result;
        while (Position < Text.size())
        {
            char Character = Text[Position++];
            if (Character == '"')
            {
                return Result;
            }
            if (static_cast<unsigned char>(Character) < 32)
            {
                throw std::runtime_error("Control character in JSON string.");
            }
            if (Character != '\\')
            {
                Result += Character;
                continue;
            }
            if (Position >= Text.size())
            {
                break;
            }
            Character = Text[Position++];
            if (Character == 'u')
            {
                unsigned Codepoint = ReadHex();
                if (Codepoint >= 0xD800 && Codepoint <= 0xDBFF)
                {
                    if (Text.compare(Position, 2, "\\u") != 0)
                    {
                        throw std::runtime_error("Missing JSON low surrogate.");
                    }
                    Position += 2;
                    const unsigned Low = ReadHex();
                    if (Low < 0xDC00 || Low > 0xDFFF)
                    {
                        throw std::runtime_error("Invalid JSON low surrogate.");
                    }
                    Codepoint = 0x10000 + ((Codepoint - 0xD800) << 10) + Low - 0xDC00;
                }
                else if (Codepoint >= 0xDC00 && Codepoint <= 0xDFFF)
                {
                    throw std::runtime_error("Unexpected JSON low surrogate.");
                }
                if (Codepoint < 0x80)
                {
                    Result += static_cast<char>(Codepoint);
                }
                else
                {
                    std::string Suffix;
                    unsigned Prefix = 0xC0;
                    unsigned Limit = 0x800;
                    while (Codepoint >= Limit && Limit < 0x200000)
                    {
                        Prefix = (Prefix >> 1) | 0x80;
                        Limit <<= 5;
                    }
                    unsigned Remaining = Codepoint;
                    do
                    {
                        Suffix.insert(Suffix.begin(), static_cast<char>(0x80 | (Remaining & 63)));
                        Remaining >>= 6;
                    } while (Remaining >= (0x100 - Prefix));
                    Result += static_cast<char>(Prefix | Remaining);
                    Result += Suffix;
                }
                continue;
            }
            switch (Character)
            {
            case 'n':
                Result += '\n';
                break;
            case 'r':
                Result += '\r';
                break;
            case 't':
                Result += '\t';
                break;
            case 'b':
                Result += '\b';
                break;
            case 'f':
                Result += '\f';
                break;
            case '\\':
            case '/':
            case '"':
                Result += Character;
                break;
            default:
                throw std::runtime_error("Invalid JSON escape.");
            }
        }
        throw std::runtime_error("Unterminated JSON string.");
    }

    FCompilationJsonValue Read(unsigned Depth = 0)
    {
        SkipSpace();
        if (Depth > 128 || Position >= Text.size())
        {
            throw std::runtime_error("Invalid compilation database JSON.");
        }
        FCompilationJsonValue Result;
        Result.Kind = Text[Position];
        if (Result.Kind == '"')
        {
            Result.Text = ReadString();
            return Result;
        }
        if (Result.Kind != '[' && Result.Kind != '{')
        {
            throw std::runtime_error("Unsupported compilation database JSON value.");
        }
        ++Position;
        char Closing = ']';
        if (Result.Kind == '{')
        {
            Closing = '}';
        }
        SkipSpace();
        if (Position < Text.size() && Text[Position] == Closing)
        {
            ++Position;
            return Result;
        }
        while (Position < Text.size())
        {
            if (Result.Kind == '{')
            {
                SkipSpace();
                const std::string Key = ReadString();
                SkipSpace();
                if (Position >= Text.size() || Text[Position++] != ':')
                {
                    throw std::runtime_error("Missing JSON property separator.");
                }
                Result.Properties.emplace(Key, Read(Depth + 1));
            }
            else
            {
                Result.Elements.push_back(Read(Depth + 1));
            }
            SkipSpace();
            if (Position < Text.size() && Text[Position] == Closing)
            {
                ++Position;
                return Result;
            }
            if (Position >= Text.size() || Text[Position++] != ',')
            {
                throw std::runtime_error("Missing JSON element separator.");
            }
        }
        throw std::runtime_error("Unterminated JSON container.");
    }
};

static void ExpandResponseArguments(const std::vector<std::string> &Arguments, const std::string &Directory,
                                    std::unordered_set<std::string> &Active, std::vector<std::string> &Result)
{
    for (const std::string &Argument : Arguments)
    {
        if (Argument.empty() || Argument.front() != '@')
        {
            Result.push_back(Argument);
            continue;
        }
        const std::string Path = NormalizeCppPath(Argument.substr(1), Directory);
        if (!Active.insert(Path).second || Active.size() > 64)
        {
            throw std::runtime_error("Recursive response file: " + Path);
        }
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream)
        {
            throw std::runtime_error("Cannot read response file: " + Path);
        }
        const std::string Contents((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
        ExpandResponseArguments(SplitCppCommand(Contents), Directory, Active, Result);
        Active.erase(Path);
    }
}

std::vector<FCppCompileCommand> FCppSourceParser::LoadCompilationDatabase(const std::string &Directory,
                                                                          std::string &Error)
{
    Error.clear();
    try
    {
        const std::string File = NormalizeCppPath("compile_commands.json", Directory);
        std::ifstream Stream(File, std::ios::binary);
        if (!Stream)
        {
            throw std::runtime_error("Cannot open compilation database: " + File);
        }
        std::string Contents((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
        if (Contents.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            Contents.erase(0, 3);
        }
        FCompilationJsonReader Reader(Contents);
        const auto Root = Reader.Read();
        Reader.SkipSpace();
        if (Root.Kind != '[' || Reader.Position != Contents.size())
        {
            throw std::runtime_error("Expected a compilation database array.");
        }
        std::vector<FCppCompileCommand> Result;
        for (const auto &Entry : Root.Elements)
        {
            FCppCompileCommand Command;
            Command.SourceFile = Entry.Properties.at("file").Text;
            Command.WorkingDirectory = NormalizeCppPath(Entry.Properties.at("directory").Text, Directory);
            std::vector<std::string> Arguments;
            const auto Array = Entry.Properties.find("arguments");
            if (Array != Entry.Properties.end())
            {
                if (Array->second.Kind != '[')
                {
                    throw std::runtime_error("Expected compiler argument array.");
                }
                for (const auto &Argument : Array->second.Elements)
                {
                    if (Argument.Kind != '"')
                    {
                        throw std::runtime_error("Expected compiler argument string.");
                    }
                    Arguments.push_back(Argument.Text);
                }
            }
            else
            {
                Arguments = SplitCppCommand(Entry.Properties.at("command").Text);
            }
            if (Command.SourceFile.empty() || Arguments.empty())
            {
                throw std::runtime_error("Empty compilation database entry.");
            }
            std::unordered_set<std::string> Active;
            ExpandResponseArguments(Arguments, Command.WorkingDirectory, Active, Command.Arguments);
            Result.push_back(std::move(Command));
        }
        return Result;
    }
    catch (const std::exception &Failure)
    {
        Error = Failure.what();
        return {};
    }
}
