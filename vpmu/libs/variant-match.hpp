#ifndef __VARIANT_MATCH_HPP_
#define __VARIANT_MATCH_HPP_
#pragma once

#include "variant.hpp"

namespace mpark
{
namespace myutils
{
    template <typename... Fns>
    struct visitor;

    template <typename Fn>
    struct visitor<Fn> : Fn {
        using Fn::operator();

        template <typename T>
        visitor(T&& fn) : Fn(std::forward<T>(fn))
        {
        }
        inline constexpr bool valueless_by_exception() const noexcept { return false; }
    };

    template <typename Fn, typename... Fns>
    struct visitor<Fn, Fns...> : Fn, visitor<Fns...> {
        using Fn::             operator();
        using visitor<Fns...>::operator();

        template <typename T, typename... Ts>
        visitor(T&& fn, Ts&&... fns)
            : Fn(std::forward<T>(fn)), visitor<Fns...>(std::forward<Ts>(fns)...)
        {
        }
        inline constexpr bool valueless_by_exception() const noexcept { return false; }
    };

    template <typename... Fns>
    visitor<typename std::decay<Fns>::type...> make_visitor(Fns&&... fns)
    {
        return visitor<typename std::decay<Fns>::type...>(std::forward<Fns>(fns)...);
    };
}

template <typename V, typename... Fs>
auto match(V&& var, Fs&&... fs)
{
    return visit(myutils::make_visitor(std::forward<Fs>(fs)...), var);
}
}
#endif
