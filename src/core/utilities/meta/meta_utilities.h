#ifndef PHARE_CORE_UTILITIES_META_META_UTILITIES_H
#define PHARE_CORE_UTILITIES_META_META_UTILITIES_H

#include <iterator>
#include <type_traits>

namespace PHARE
{
namespace core
{
    template<typename...>
    using tryToInstanciate = void;


    struct dummy
    {
        using type              = int;
        static const type value = 0;
    };




    template<typename IterableCandidate, typename AttemptBegin = void, typename AttemptEnd = void>
    struct has_beginend : std::false_type
    {
    };



    /** \brief has_beginend is a traits that permit to check if a Box or a BoxContainer
     * is passed as template argument
     */
    template<typename IterableCandidate>
    struct has_beginend<IterableCandidate,
                        tryToInstanciate<decltype(std::begin(std::declval<IterableCandidate>()))>,
                        tryToInstanciate<decltype(std::end(std::declval<IterableCandidate>()))>>
        : std::true_type
    {
    };


    template<typename IterableCandidate>
    using is_iterable = std::enable_if_t<has_beginend<IterableCandidate>::value, dummy::type>;



    // Basic function
    template<typename T>
    constexpr void allsame(T)
    {
    }

    // Recursive function
    template<typename T, typename T2, typename... Ts,
             typename = std::enable_if_t<std::is_same<T, T2>::value>>
    constexpr void allsame(T arg, T2 arg2, Ts... args)
    {
        allsame(arg2, args...);
    }
} // namespace core

} // namespace PHARE




#endif
