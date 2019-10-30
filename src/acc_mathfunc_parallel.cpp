#include "mathfunc_parallel.h"


TINKER_NAMESPACE_BEGIN
namespace parallel {
template <class T>
T reduce_sum(const T* gpu_a, size_t cpu_n)
{
   T val = 0;
   #pragma acc parallel loop deviceptr(gpu_a) reduction(+:val)
   for (size_t i = 0; i < cpu_n; ++i)
      val += gpu_a[i];
   return val;
}
template int reduce_sum(const int*, size_t);
template float reduce_sum(const float*, size_t);
template double reduce_sum(const double*, size_t);
template unsigned long long reduce_sum(const unsigned long long*, size_t);


template <class HT, size_t HN, class DPTR>
void reduce_sum2(HT (&RESTRICT h_ans)[HN], DPTR RESTRICT v, size_t nelem)
{
   typedef typename deduce_ptr<DPTR>::type CONST_DT;
   typedef typename std::remove_const<CONST_DT>::type DT;
   static_assert(std::is_same<HT, DT>::value, "");

   constexpr size_t neach = deduce_ptr<DPTR>::n;
   static_assert(HN <= neach, "");

   for (size_t iv = 0; iv < HN; ++iv) {
      HT ans = 0;
      #pragma acc parallel loop deviceptr(v) reduction(+:ans)
      for (size_t ig = 0; ig < nelem; ++ig)
         ans += v[ig][iv];
      h_ans[iv] = ans;
   }
}
template void reduce_sum2(int (&)[6], int (*)[8], size_t);
template void reduce_sum2(float (&)[6], float (*)[8], size_t);
template void reduce_sum2(double (&)[6], double (*)[8], size_t);
template void reduce_sum2(unsigned long long (&)[6], unsigned long long (*)[8],
                          size_t);


template <class T>
T dotprod(const T* RESTRICT gpu_a, const T* RESTRICT gpu_b, size_t cpu_n)
{
   T val = 0;
   #pragma acc parallel loop deviceptr(gpu_a,gpu_b) reduction(+:val)
   for (size_t i = 0; i < cpu_n; ++i)
      val += gpu_a[i] * gpu_b[i];
   return val;
}
template int dotprod(const int*, const int*, size_t);
template float dotprod(const float*, const float*, size_t);
template double dotprod(const double*, const double*, size_t);


template <class T>
void scale_array(T* gpu_dst, T scal, size_t nelem)
{
   #pragma acc parallel loop deviceptr(gpu_dst)
   for (size_t i = 0; i < nelem; ++i) {
      gpu_dst[i] *= scal;
   }
}
template void scale_array(float*, float, size_t);
template void scale_array(double*, double, size_t);
}
TINKER_NAMESPACE_END