/******************************************************************************/
/*  Copyright (c) 2017, Juha Reunanen <juha.reunanen@tomaattinen.com>         */
/*                                                                            */
/*  Permission to use, copy, modify, and/or distribute this software for any  */
/*  purpose with or without fee is hereby granted, provided that the above    */
/*  copyright notice and this permission notice appear in all copies.         */
/*                                                                            */
/*  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES  */
/*  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF          */
/*  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR   */
/*  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES    */
/*  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN     */
/*  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF   */
/*  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.            */
/******************************************************************************/

#ifndef _shared_lru_cache_using_std_ 
#define _shared_lru_cache_using_std_ 

#include "lru_cache_using_std.h"
#include <unordered_set>
#include <mutex>
#include <thread>

// A thread-safe variant of lru_cache_using_std that
// remains available for reading when the function is
// being evaluated.
// MAP should be one of std::map or std::unordered_map. 
template <
    typename K,
    typename V,
    template<typename...> class MAP
> class shared_lru_cache_using_std
{
public:

    typedef K key_type;
    typedef V value_type;
    typedef std::function<value_type(const key_type&)> function_type;

    // Constructor specifies the cached function and 
    // the maximum number of records to be stored 
    shared_lru_cache_using_std(
        function_type f,
        size_t c
    )
        : _underlying_lru_cache(f, c)
        , _fn(f)
    {
    }

    // Obtain value of the cached function for k 
    value_type operator()(const key_type& k) {
        {
            std::lock_guard<std::mutex> guard(_underlying_lru_cache_mutex);
            if (_underlying_lru_cache.has(k)) {
                {
                    std::lock_guard<std::mutex> guard(_hit_rate_mutex);
                    ++_hit_rate.calls;
                    ++_hit_rate.hits;
                }
                return _underlying_lru_cache.operator()(k);
            }
            else {
                std::lock_guard<std::mutex> guard(_hit_rate_mutex);
                ++_hit_rate.calls;
            }
        }

        const auto this_thread_id = std::this_thread::get_id();

        std::shared_ptr<std::mutex> key_specific_mutex = nullptr;

        {
            std::lock_guard<std::mutex> guard(_is_being_evaluated_mutex);
            auto i = _is_being_evaluated.find(k);
            if (i == _is_being_evaluated.end()) {
                _is_being_evaluated[k].mutex = std::shared_ptr<std::mutex>(new std::mutex);
                i = _is_being_evaluated.find(k);
            }
            key_specific_mutex = i->second.mutex;
            assert(i->second.active_threads.find(this_thread_id) == i->second.active_threads.end());
            i->second.active_threads.insert(this_thread_id);
        }

        const auto done = [&]() {
            std::lock_guard<std::mutex> guard(_is_being_evaluated_mutex);
            auto i = _is_being_evaluated.find(k);
            assert(i->second.active_threads.find(this_thread_id) != i->second.active_threads.end());
            i->second.active_threads.erase(this_thread_id);
            if (i->second.active_threads.empty()) {
                _is_being_evaluated.erase(i);
            }
        };

        std::lock_guard<std::mutex> evaluation_guard(*key_specific_mutex);
        {
            std::lock_guard<std::mutex> guard(_underlying_lru_cache_mutex);
            if (_underlying_lru_cache.has(k)) {
                const value_type v = _underlying_lru_cache.operator()(k);
                done();

                std::lock_guard<std::mutex> guard(_hit_rate_mutex);
                ++_hit_rate.late_hits;

                return v;
            }
        }

        const value_type v = _fn(k);

        {
            std::lock_guard<std::mutex> guard(_underlying_lru_cache_mutex);
            assert(!_underlying_lru_cache.has(k));
            _underlying_lru_cache.set(k, v);
        }

        done();
        return v;
    }

    // Find out if the cache already has some value
    // NOTE: not thread-safe at the moment! (for perf reasons)
    bool has(const key_type& k) const {
        return _underlying_lru_cache.has(k);
    }

    struct hit_rate {
        size_t calls = 0;
        size_t hits = 0;
        size_t late_hits = 0;
    };

    hit_rate get_hit_rate() const {
        std::lock_guard<std::mutex> guard(_hit_rate_mutex);
        return _hit_rate;
    }

private:

    typedef lru_cache_using_std<key_type, value_type, MAP> lru_cache_type;

    // The underlying, non-thread-safe LRU cache
    lru_cache_type _underlying_lru_cache;

    // This mutex guards the underlying LRU cache
    std::mutex _underlying_lru_cache_mutex;

    // The function to be cached 
    const function_type _fn;

    struct is_being_evaluated {
        std::shared_ptr<std::mutex> mutex;
        std::unordered_set<std::thread::id> active_threads;
    };

    typedef MAP<
        key_type,
        is_being_evaluated
    > key_to_activity;

    key_to_activity _is_being_evaluated;

    // This mutex guards the _is_being_evaluated object
    std::mutex _is_being_evaluated_mutex;

    hit_rate _hit_rate;

    mutable std::mutex _hit_rate_mutex;
};

#endif // _shared_lru_cache_using_std_
