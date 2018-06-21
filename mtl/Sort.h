#ifndef Minisat_Sort_h
#define Minisat_Sort_h

#include "mtl/Vec.h"

//=================================================================================================
// Some sorting algorithms for vec's


namespace CDCL {

template<class T>
struct LessThan_default {
    bool operator () (T x, T y) { return x < y; }
};


template <class T, class LessThan>
void selectionSort(T* array, int size, LessThan lt)
{
    int     i, j, best_i;
    T       tmp;

    for (i = 0; i < size-1; i++){
        best_i = i;
        for (j = i+1; j < size; j++){
            if (lt(array[j], array[best_i]))
                best_i = j;
        }
        tmp = array[i]; array[i] = array[best_i]; array[best_i] = tmp;
    }
}
template <class T> static inline void selectionSort(T* array, int size) {
    selectionSort(array, size, LessThan_default<T>()); }

template <class T, class LessThan>
void sort(T* array, int size, LessThan lt)
{
    if (size <= 15)
        selectionSort(array, size, lt);

    else{
        T           pivot = array[size / 2];
        T           tmp;
        int         i = -1;
        int         j = size;

        for(;;){
            do i++; while(lt(array[i], pivot));
            do j--; while(lt(pivot, array[j]));

            if (i >= j) break;

            tmp = array[i]; array[i] = array[j]; array[j] = tmp;
        }

        sort(array    , i     , lt);
        sort(&array[i], size-i, lt);
    }
}
template <class T> static inline void sort(T* array, int size) {
    sort(array, size, LessThan_default<T>()); }


//=================================================================================================
// For 'vec's:


template <class T, class LessThan> void sort(vec<T>& v, LessThan lt) {
    sort((T*)v, v.size(), lt); }
template <class T> void sort(vec<T>& v) {
    sort(v, LessThan_default<T>()); }


//=================================================================================================
}

#endif
