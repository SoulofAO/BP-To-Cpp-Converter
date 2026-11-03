/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

// Explicit instantiation permits obtaining a member without changing its declaring class.
template<typename Access, typename Access::Member Member>
struct TStructPrivateMemberAccess
{
    friend typename Access::Member GetStructPrivateMember(Access)
    {
        return Member;
    }
};
