#pragma once
#include "gen_unit.h"
#include "rc_man.h"


TINKER_NAMESPACE_BEGIN
/**
 * \ingroup spatial
 * \brief
 * Data structures and procedures to construct the spatial decomposition
 * in O(N) time complexity.
 *
 * ### A. Concepts
 *    - Boxes (`x`): The periodic boundary box ("the big box") is partitioned
 *      into smaller boxes. The ranges of the **fractional** coordinates are all
 *      `[-0.5, 0.5)`. Along x, y, and z axes, each direction is equally split
 *      into \f$ 2^{px} \f$, \f$ 2^{py} \f$, and \f$ 2^{pz} \f$ parts,
 *      respectively, where each small box contains no more than 32 atoms.
 *      Range: `[0, nx)`.
 *    - `nx`: Number of boxes.
 *    - SortedAtoms (`a`, Atoms for short): Atom numbers sorted by their box
 *      number; Range: `[0, n)`.
 *    - `n`: Number of (sorted) atoms.
 *    - BLOCK: A constant set to 32, which happens to be the number of bits in
 *      a 32-bit integer and CUDA warp size.
 *    - Blocks (`k`): Any 32 (or fewer) consecutively stored data.
 *       - BoxBlock (`xk`). Number of BoxBlocks (`nxk`).
 *       - AtomBlock (`ak`). Number of AtomBlocks (`nak`).
 *    - Flags (`f`): 32-bit integers. The i-th bit is un/set means the i-th
 *      bit is set to 0/1.
 *    - POPC: An operation to count the number of bits set in a 32-bit flag.
 *      For more details, see:
 *       - `__builtin_popcount` available in GCC C/C++.
 *       - `__popc` available in NVCC CUDA.
 *       - `__popcnt` available in MSVC C++.
 *       - `POPCNT` available in Fortran.
 *    - FFS and FFSN: An operation to find the position of the i-th (1st for
 *      FFS) least significant bit set in a 32-bit flag.
 *       - E.g., FFS(0x042) = 2; FFSN(0x042, 2) = 7.
 *       - `i` and the returned answer are 1-based.
 *       - FFS and POPC are the building blocks of FFSN.
 *       - `int __builtin_ffs` available in GCC C/C++.
 *       - `int __ffs` available in NVCC CUDA.
 *       - `_BitScanForward` available in MSVC C++.
 *    - Scan: An operation to obtain the partial sum of a given array.
 *       - InclusiveScan: \f$ Output\ (k) = \sum_0^k Input\ (k). \f$
 *       - ExclusiveScan: \f$ Output\ (k) = \sum_0^{k-1} Input\ (k). \f$
 *       - Scan operations can be in-place, meaning the input array will also
 *         be used as the output array. Partially overlapped input and output
 *         arrays are **PROHIBITED**.
 *
 * ### B. Sort the Atoms
 *    1. Zero out the `ax_scan` array (see **D**).
 *    2. Before sorting, the `sorted[n]` array is initialized by the the
 *       fractional coordinates and the original atom number of the unsorted
 *       atoms.
 *    3. In the mean time, `boxnum[n]` array is initialized by the box to which
 *       the unsorted atom belongs.
 *    4. The `nax[nx]` array is also updated while the box numbers are
 *       calculated.
 *    5. Swap the atoms as the box numbers are being sorted.
 *
 * \image html doc/spatial_a.svg "Fig. 1  SortedAtoms and Box-AtomBlock-Flags"
 *
 * ###C. Nearby boxes (near and nearby[nx])
 * These two work together as a "template" to find the nearby boxes.
 *
 * Fixing box 0 as box `j`:
 *    1. Iterate all of the boxes, including box `j` itself, as box `i`.
 *       If boxes `j` and `i` are close enough (less than or equal to cutoff,
 *       maybe plus buffer), set `nearby[i]` to `i`, or -1 otherwise.
 *    2. Squish all of the -1 out of the `nearby` array.
 *    3. Return an index number `near` such that the remaining `i` values are
 *       stored consecutively from `nearby[0]` to `nearby[near-1]`.
 *
 * Procedure 1 and 2 are all parallel algorithms.
 *
 * ### D. Atom-Box-Scan (ax_scan[nax+1])
 *
 * \image html doc/spatial_x.svg "Fig. 2  Boxes and Atom-Box-Scan"
 *
 * Suppose there are three temporary arrays `nax[nx]`, `escan[nx]`, and
 * `iscan[nx]`. `nax` stores that there are `nax[i]` atoms belong to box `i`.
 * `escan = ExclusiveScan(nax)`, `iscan = InclusiveScan(nax)`. We can
 *    - calculate the number of sorted atoms belong to box `i` by:
 *       - `nax[i]`;
 *       - or `iscan[i] - escan[i]`.
 *    - obtain the range of the sorted atoms in the form of `[begin, end)`:
 *       - `[escan[i], iscan[i])`.
 *
 * These three temporary arrays can be merged into one array `ax_scan[nx+1]` by:
 *    1. Zero out all of its elements (B.1).
 *    2. Use `ax_scan[1:]` as `nax[0:]` in the counting kernel (B.4).
 *    3. In-place `InclusiveScan(nax[0:])`.
 *    4. Use `ax_scan[0:]` as `escan`; use `ax_scan[1:]` as `iscan`.
 *
 * ### E. Box-AtomBlock-Flag (xakf[nak])
 * Atom number `a(j,i)=(j+i*BLOCK)` corresponds to the j-th bit of flag
 * `xakf[i]`.
 *    1. The bit `(j,i)` is set to 1 if SortedAtoms `(j,i)` and `(j-1,i)`
 *       belong to different boxes.
 *    2. If `j=0`, `j-1` equals 31 (`0-1+BLOCK`).
 *    3. If `(j+i*BLOCK)>=n`, the "padded" atoms and atom `n-1` are always in
 *       the same box.
 *    4. If all of the atoms in atom block `i` are in the same box, `xakf[i]` is
 *       set to 1 (`0x01`).
 *    5. (CUDA) Given a local variable `var` which may have different values in
 *       other threads, if thread `i` and `j` are in the same warp,
 *       `__shfl_sync` can retrieve `var` in thread `j` from thread `i`, and
 *       vice versa.
 *    6. (CUDA) *Generally*, calling `__ballot_sync` with `val` will return a
 *       32-bit flag, whose k-th bit is set if the `val` is true for the k-th
 *       thread of the warp.
 *    7. Number of unique boxes for AtomBlock `i`: `POPC(xakf[i])`.
 *    8. E.g, if `POPC(xakf[i])>=2`, the 2nd unique box number for AtomBlock
 *       `i`: `boxnum[j+i*BLOCK]` with `j = FFSN(xakf[i],2)-1`.
 *
 * ### F. Box-AtomBlock-Flag-Scan (xak_sum and xakf_scan[nak])
 *    1. xak_sum: `sum(POPC(xakf))`.
 *    2. xakf_scan: `ExclusionScan(POPC(xakf))`.
 *    3. For instance, `near` equals 55. To find out all of the neighbors of
 *       the atoms from AtomBlock 0 to `nak-1`, we need to consider candidates
 *       from at most `55*xak_sum` spatial boxes.
 *    4. As each spatial box does not contain more than 32 atoms, the maximum
 *       preallocated array size for the neighbors is `32*55*xak_sum`.
 *    5. `xakf_scan` stores the "offset indices" of the neighbors of AtomBlocks.
 *       For AtomBlock `i`, its first first neighbor will appear in
 *       `array[32*55*xakf_scan[i]]`.
 *    6. The maximum number of neighbors for AtomBlock `i` equals
 *       `32*55*POPC(xakf[i])`.
 *
 * ### G. Neighbors of AtomBlocks
 *    1. `niak`, `iak`, and `lst` are the "output" data structures.
 *    2. Consider an atom pair `(i,j)` and the classical Verlet list
 *       `lst[n][max]` and `nlst[n]`. The `i` atoms are implicitly represented
 *       by the array indices to iterate from `lst[0]` to `lst[n-1]`. Atom `j`
 *       is stored in `lst[i][:]`. Iterating from 0 to `nlst[i]-1` will
 *       definitely encounter atom `j`.
 *    3. In this implementation, the allocated length for `lst` is at least
 *       `32*near*xak_sum`, and `near*xak_sum` for `iak`. (F.3 and F.4)
 *    4. Starting from `near*xakf_scan[i]`, `near*POPC(xakf[i])` elements in
 *       `iak` are assigned to `i` for AtomBlock `i`.
 *    5. Starting from `32*near*xakf_scan[i]`, `32*near*POPC(xakf[i])` elements
 *       in `lst` are reserved, which store the neighbor atom numbers for
 *       AtomBlock `i`. For details, see **H**.
 *    6. The unfilled elements of `lst` are set to zeros.
 *    7. Check every 32 integers in `lst`. If starting from `lst[32*j]`, all of
 *       the 32 elements are zeros, remove them from `lst` and remove `iak[j]`
 *       from `iak` as well. `niak` elements are left in `iak` and `32*niak`
 *       elements are left in `lst`.
 *
 * ### H. Details in Finding Neighbors
 *    1. `naak[nak]` and `xkf[nak*nxk]` are the internal data structures.
 *    2. `lst` is calculated in parallel; each AtomBlock adopts 32 threads to
 *       iterate `near*POPC(xakf[i])` nearby boxes. There are `POPC(xakf[i])`
 *       boxes in AtomBlock `i` as "box 1", and each "box 1" has `near` nearby
 *       boxes ("box 2"). Different "box 1" may share a few "box 2".
 *    3. `xkf[i*nxk:(i+1)*nxk]` (`32*nxk` bits in total) are used to eliminate
 *       the duplication of "box 2" for AtomBlock `i` that if the k-th bit is
 *       set, atoms in box `k` should be considered as neighbors of AtomBlock
 *       `i`.
 *    4. Suppose atoms in box `j` will be copied to `lst` for AtomBlock `i`.
 *       The sorted atom numbers are in the range of `[escan[j], iscan[j])`
 *       (D.4), and more importantly, only atom numbers **GREATER** than the
 *       minimum atom number of AtomBlock `i` (which equals `32*i`) are valid
 *       neighbors.
 *    5. (Continued.) Therefore, the range `[begin, end)` is adjusted so that
 *       `begin=max(32*i+1,escan[j])`. The adjusted length of the range (`len`)
 *       is `(iscan[j]-begin)`.
 *    6. `naak[i]` stores the number of neighbors for AtomBlock `i`. Since
 *       `len` can be negative, the increment is `max(0, len)`.
 */
struct Spatial
{
   /**
    * \brief Sorted atoms, containing the fractional x, y, and z coordinates,
    * and the unsorted atom number.
    */
   struct SortedAtom
   {
      real fx, fy, fz;
      int unsorted;
#if TINKER_DOUBLE_PRECISION
      int padding_;
#endif
   };
   static constexpr int BLOCK = 32;


   // output
   int niak;
   // internal
   int n, nak;
   int px, py, pz, nx, nxk;
   int near, xak_sum, xak_sum_cap;


   // output
   int *iak, *lst;
   // internal
   SortedAtom* sorted;           // n
   int* boxnum;                  // n
   int *naak, *xakf, *xakf_scan; // nak
   int* nearby;                  // nx
   int* ax_scan;                 // nx + 1
   int* xkf;                     // nax * nxk


   ~Spatial();
};
using SpatialUnit = GenericUnit<Spatial, GenericUnitVersion::EnableOnDevice>;
TINKER_EXTERN SpatialUnit vspatial_unit;
TINKER_EXTERN SpatialUnit mspatial_unit;


void spatial_data(rc_op);
TINKER_NAMESPACE_END
/// \defgroup spatial Spatial Decomposition