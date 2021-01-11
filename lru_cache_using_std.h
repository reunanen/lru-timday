/******************************************************************************/
/*  Copyright (c) 2010-2011, Tim Day <timday@timday.com>                      */
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

#ifndef _lru_cache_using_std_ 
#define _lru_cache_using_std_ 

#include <cassert> 
#include <list>
#include <functional> // for std::function

// Class providing fixed-size (by number of records) 
// LRU-replacement cache of a function with signature 
// V f(K). 
// MAP should be one of std::map or std::unordered_map. 
// Variadic template args used to deal with the 
// different type argument signatures of those 
// containers; the default comparator/hash/allocator 
// will be used. 
template <
    typename K,
    typename V,
    template<typename...> class MAP
> class lru_cache_using_std
{
public:

    typedef K key_type;
    typedef V value_type;

    // Key access history, most recent at back 
    typedef std::list<key_type> key_tracker_type;

    // Key to value and key history iterator 
    typedef MAP<
        key_type,
        std::pair<
        value_type,
        typename key_tracker_type::iterator
        >
    > key_to_value_type;

    typedef std::function<value_type(const key_type&)> function_type;

    // Constructor specifies the cached function and 
    // the maximum number of records to be stored 
    lru_cache_using_std(
        function_type f,
        size_t c
    )
        : _fn(f)
        , _capacity(c)
    {
        assert(_capacity != 0);
    }

    // Obtain value of the cached function for k 
    value_type operator()(const key_type& k) {

        // Attempt to find existing record 
        const typename key_to_value_type::iterator it
            = _key_to_value.find(k);

        if (it == _key_to_value.end()) {

            // We don't have it: 

            // Evaluate function and create new record 
            const value_type v = _fn(k);
            insert(k, v);

#ifndef NDEBUG
            // Update evaluation counters
            // - do it like this instead of a simple one-liner
            //   ("++_eval_counters[k]"), because now it's
            //   convenient to add a breakpoint for unexpected
            //   cache misses (counter increased beyond 1)
            const auto i = _eval_counters.find(k);
            if (i != _eval_counters.end()) {
                ++i->second;
            }
            else {
                _eval_counters[k] = 1;
            }
#endif // #ifndef NDEBUG

            // Return the freshly computed value 
            return v;

        }
        else {

            // We do have it: 

            // Update access record by moving 
            // accessed key to back of list 
            _key_tracker.splice(
                _key_tracker.end(),
                _key_tracker,
                (*it).second.second
            );

            // Return the retrieved value 
            return (*it).second.first;
        }
    }

    // Obtain the cached keys, most recently used element 
    // at head, least recently used at tail. 
    // This method is provided purely to support testing. 
    template <typename IT> void get_keys(IT dst) const {
        typename key_tracker_type::const_reverse_iterator src
            = _key_tracker.rbegin();
        while (src != _key_tracker.rend()) {
            *dst++ = *src++;
        }
    }

    // Using the functions has() and set(), it is possible to
    // build a thread-safe cache without having to lock the
    // whole cache in order to evaluate (and keep) a new value.

    // Find out if the cache already has some value
    bool has(const key_type& k) const {
        return _key_to_value.find(k) != _key_to_value.end();
    }

    // Set a key-value pair that may be missing in the cache
    void set(const key_type& k, const value_type& v) {
        const auto i = _key_to_value.find(k);
        if (i == _key_to_value.end()) {
            insert(k, v);
        }
        else {
            // If we already have a value, it would be logical
            // to assume that it is equal to whatever we tried
            // to set

            //assert(i->second == v);

            // However, the above assertion has been commented
            // out for now, because we don't want to require
            // that the value type has an equality operator.
            // TODO: Use SFINAE to enable the assertion when
            // the value type does have an equality operator.
        }
    }

    // It is sometimes convenient to be able to free memory, when you
    // know that the item won't be needed any longer
    void evict(const key_type& key) {

        // Let's see if the item exists
        const typename key_to_value_type::iterator it = _key_to_value.find(key);

        if (it != _key_to_value.end()) {
            // Erase both elements to completely purge record 
            _key_to_value.erase(it);

            const auto it2 = std::find(_key_tracker.begin(), _key_tracker.end(), key);
            assert(it2 != _key_tracker.end());
            _key_tracker.erase(it2);
        }
    }

    bool is_full() const {
        return _key_to_value.size() >= _capacity;
    }

private:

    // Record a fresh key-value pair in the cache 
    void insert(const key_type& k, const value_type& v) {

        // Method is only called on cache misses 
        assert(_key_to_value.find(k) == _key_to_value.end());

        // Make space if necessary 
        if (is_full())
            evict();

        // Record k as most-recently-used key 
        typename key_tracker_type::iterator it
            = _key_tracker.insert(_key_tracker.end(), k);

        // Create the key-value entry, 
        // linked to the usage record. 
        _key_to_value.insert(
            std::make_pair(
                k,
                std::make_pair(v, it)
            )
        );
        // No need to check return, 
        // given previous assert. 
    }

    // Purge the least-recently-used element in the cache 
    void evict() {

        // Assert method is never called when cache is empty 
        assert(!_key_tracker.empty());

        // Identify least recently used key 
        const typename key_to_value_type::iterator it
            = _key_to_value.find(_key_tracker.front());
        assert(it != _key_to_value.end());

        // Erase both elements to completely purge record 
        _key_to_value.erase(it);
        _key_tracker.pop_front();
    }

    // The function to be cached 
    const function_type _fn;

    // Maximum number of key-value pairs to be retained 
    const size_t _capacity;

    // Key access history 
    key_tracker_type _key_tracker;

    // Key-to-value lookup 
    key_to_value_type _key_to_value;

#ifndef NDEBUG
    // Evaluation counters
    MAP<key_type, size_t> _eval_counters;
#endif
};

#endif
