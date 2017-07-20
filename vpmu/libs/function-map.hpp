#ifndef _FUNCTION_MAP_HPP_
#define _FUNCTION_MAP_HPP_
#pragma once

#include <map>
#include <functional>

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// K: Key, ... Argument list
// If you want a reference type, please specify it in template argument
template <class K, class... Args>
class FunctionMap
{
private:
    struct FunctionEntry {
        std::function<void(Args...)> pre_call;
        std::function<void(Args...)> on_call;
        std::function<void(Args...)> on_return;

        bool called_flag;
    };

public:
    // Call with given return address(key)
    bool call(K key, K key_ret, Args... args)
    {
        auto f = funs.find(key);
        if (f != funs.end()) {
            auto& entry = f->second;
            // Register return function if there is one
            if (unlikely(entry.on_return)) {
                entry.called_flag = true;
                funs_ret[key_ret] = &entry;
            }
            if (unlikely(entry.pre_call)) {
                entry.pre_call(std::forward<Args>(args)...);
            }
            // Call only when the callback function is defined
            if (unlikely(entry.on_call)) {
                entry.on_call(std::forward<Args>(args)...);
                return true;
            }
        }
        return false;
    }

    // Simply call
    bool call(K key, Args... args)
    {
        auto f = funs.find(key);
        if (f != funs.end()) {
            auto& entry = f->second;
            // Call only when the callback function is defined
            if (unlikely(entry.pre_call)) {
                entry.pre_call(std::forward<Args>(args)...);
            }
            if (unlikely(entry.on_call)) {
                entry.on_call(std::forward<Args>(args)...);
                return true;
            }
        }
        return false;
    }

    bool call_return(K key, Args... args)
    {
        auto f = funs_ret.find(key);
        if (f != funs_ret.end()) {
            auto& entry = *(f->second);
            // Call only when the callback function is defined
            if (unlikely(entry.called_flag && entry.on_return)) {
                entry.on_return(std::forward<Args>(args)...);
                entry.called_flag = false;
                return true;
            }
        }
        return false;
    }

    void register_precall(K key, std::function<void(Args...)> fun)
    {
        funs[key].pre_call = fun;
    }

    void register_call(K key, std::function<void(Args...)> fun)
    {
        funs[key].on_call = fun;
    }

    void register_return(K key, std::function<void(Args...)> fun)
    {
        funs[key].on_return = fun;
    }

    bool find(K key)
    {
        auto f = funs.find(key);
        if (f != funs.end()) {
            return true;
        } else {
            return false;
        }
    }

private:
    std::map<K, struct FunctionEntry>  funs;
    std::map<K, struct FunctionEntry*> funs_ret;
};

#endif
