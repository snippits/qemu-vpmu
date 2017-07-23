#ifndef __FUNCTION_MAP_HPP_
#define __FUNCTION_MAP_HPP_
#pragma once

#include <map>
#include <functional>

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

/// @brief Callback functions associated with keys
/// @details This class helps to implement the callbacks for both of user processes
/// and Linux kernel. Three events are provided, pre_call(), on_call(), and on_return().
/// User needs to specify the type of Key and all arguments of the callback function
/// with template in order to use this class.
/// If you want a reference type, please specify it in template arguments.
///
/// Examples:
/*! @code
// key (K): uint64_t, callback args (Args): char*

void register_my_callbacks(auto& functions) {
    uint64_t addr = 1; // Assign the address (key) of the function

    functions.register_call(addr, [](const char* msg) {
        printf("On call: %s\n", msg);
        return ; // no return value.
    });

    // The return function is bound to the function address, not the return address.
    functions.register_return(addr, [](const char* msg) {
        printf("On return: %s\n", msg);
        return ; // no return value.
    });
}

int main(int argc, char* argv[]) {
    FunctionMap<uint64_t, const char*> functions;
    register_my_callbacks(functions);

    // Call without giving/assigning the return event.
    functions.call_no_ret(1, "no assign");
    functions.call_return(1, "not gonna be called");
    // Call with giving/assigning the return event.
    functions.call(1, 2, "assign key of return to 2");
    functions.call_return(2, "hello world!");

    return 0;
}
@endcode
*/
template <class K, class... Args>
class FunctionMap
{
private:
    /// @brief The entry of callbacks associated with key (i.e. PC address)
    struct FunctionEntry {
        std::function<void(Args...)> pre_call;
        std::function<void(Args...)> on_call;
        std::function<void(Args...)> on_return;

        bool called_flag;
    };

public:
    /// @brief Call with given return key (i.e. PC address)
    /// @param[in] key The key to "function address".
    /// @param[in] key_ret The key to "return address".
    /// @param[in] args The arguments passed to the callback function.
    /// @return true on success. false when no function found/executed.
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

    /// @brief Call without creating return events
    /// @param[in] key The key to "function address".
    /// @param[in] args The arguments passed to the callback function.
    /// @return true on success. false when no function found/executed.
    bool call_no_ret(K key, Args... args)
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

    /// @brief Call the callback for function return with the key of "return".
    /// @param[in] key The key to "return address" not the "function address".
    /// @param[in] args The arguments passed to the callback function.
    /// @return true on success. false when no function found/executed.
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

    /// @brief Register the callback function/lambda for target key (i.e. PC address).
    void register_precall(K key, std::function<void(Args...)> fun)
    {
        funs[key].pre_call = fun;
    }

    /// @brief Register the callback function/lambda for target key (i.e. PC address).
    void register_call(K key, std::function<void(Args...)> fun)
    {
        funs[key].on_call = fun;
    }

    /// @brief Register the callback function/lambda for target key (i.e. PC address).
    void register_return(K key, std::function<void(Args...)> fun)
    {
        funs[key].on_return = fun;
    }

    /// @brief Find if the key is registered with anything.
    bool find(K key) { return (funs.find(key) != funs.end()); }

private:
    /// The mapping table for on call events
    std::map<K, struct FunctionEntry> funs;
    /// The mapping table for on return events, it takes the pointers of FunctionEntry.
    std::map<K, struct FunctionEntry*> funs_ret;
};

#endif
