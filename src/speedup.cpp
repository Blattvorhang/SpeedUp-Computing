#include <omp.h>        // OpenMP
#include <immintrin.h>  // SIMD
#include <math.h>
#include <thread>
#include "common.h"

#define SSE 0

extern RunningMode mode;


#if SSE
/* SSE version */
float sumSpeedUp(const float data[], const int len) {
    __m128 sum_value_sse = _mm_setzero_ps();
    float sum_value[4] = {0};
    float access_value[4];
    int i;

    //#pragma omp parallel for num_threads(MAX_THREADS) reduction(_mm512_add_ps:sum_value_sse)
    for (i = 0; i < len - 3; i += 4) {
        // for (int j = 0; j < 4; j++) {
        //     access_value[j] = ACCESS(data[i + j]);
        // }
        // __m128 data_sse = _mm_loadu_ps(access_value);
        __m128 data_sse = _mm_loadu_ps(data + i);
        __m128 access_sse = _mm_log_ps(_mm_sqrt_ps(data_sse));
        sum_value_sse = _mm_add_ps(sum_value_sse, data_sse);
    }
    
    _mm_storeu_ps(sum_value, sum_value_sse);

    for (; i < len; i++) {
        sum_value[0] += ACCESS(data[i]);
    }

    float final_sum = 0;
    for (int i = 0; i < 4; i++) {
        final_sum += sum_value[i];
    }
    return final_sum;
}


#else
/* OpenMP version */
float sumSpeedUp(const float data[], const int len) {
    double sum_value = 0;
    #pragma omp parallel for num_threads(MAX_THREADS) reduction(+:sum_value)
    for (int i = 0; i < len; i++) {
        sum_value += ACCESS(data[i]);
    }
    return float(sum_value);
}
#endif


template <typename T>
inline T max(T a, T b) {
    return a > b ? a : b;
}


float maxSpeedUp(const float data[], const int len) {
    if (len <= 0)
        return 0;
    double max_value = ACCESS(data[0]);
    #pragma omp parallel for num_threads(MAX_THREADS) reduction(max:max_value)
    for (int i = 1; i < len; i++) {
        double temp = ACCESS(data[i]);
        if (temp > max_value) {
            max_value = temp;
        }
    }
    return float(max_value);
}


inline int log2(int x) {
    int result = 0;
    while (x >>= 1)
        result++;
    return result;
}


/**
 * @brief This is an auxiliary function for parallelSort. It sorts the input array in-place.
 *        the result is stored alternately in arr1 and arr2 for faster merging.
 * @param arr1 The array to be sorted.
 * @param arr2 The array to be sorted.
 * @param len The length of the array.
 * @param level The level of parallelism. If level is 0, the function falls back to regular sort.
 */
void parallelSortAux(float arr1[], float arr2[], const int len, const int level) {
    if (level == 0) {
        mergeSort(arr1, len);
        return;
    }

    const int mid = len / 2;
    std::thread t1(parallelSortAux, arr1, arr2, mid, level - 1);
    std::thread t2(parallelSortAux, arr1 + mid, arr2 + mid, len - mid, level - 1);

    /* wait for the two arrays to be sorted */
    t1.join();
    t2.join();

    /* merge the two halves alternately */
    if (level % 2)
        merge(arr2, arr1, arr1 + mid, mid, len - mid);
    else
        merge(arr1, arr2, arr2 + mid, mid, len - mid);
}


/**
 * @brief This function sorts the input array in-place. It splits the array into two halves, 
 *        and sorts each half in a separate thread. Then, it merges the two halves back together.
 * @param arr The array to be sorted.
 * @param len The length of the array.
 * @param level The level of parallelism. If level is 0, the function falls back to regular sort.
 */
void parallelSort(float arr[], const int len, const int level) {
    float *result = arr;
    float *temp = new float[len];
    parallelSortAux(arr, temp, len, level);
    if (level % 2) {
        for (int i = 0; i < len; i++)
            arr[i] = temp[i];
    }
}


void sortSpeedUp(const float data[], const int len, float result[]) {
    if (mode == LOCAL) {
        for (int i = 0; i < len; i++)
            result[i] = data[i];
        parallelSort(result, len, log2(MAX_THREADS) - 1);
    }
}