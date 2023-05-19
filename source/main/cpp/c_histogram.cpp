#include "ccore/c_target.h"
#include "cbase/c_integer.h"
#include "cbase/c_limits.h"
#include "cbase/c_memory.h"
//#include "cfile/c_file.h"

#include "chistogram/c_histogram.h"

#include <math.h>

namespace ncore
{
    namespace nhdr
    {
        const s32 EINVAL = -1;
        const s32 ENOMEM = -2;
        const s32 EIO = -3;

        void* hdr_calloc(s32 count, s32 size)
        {
            return nullptr;
        }

        void hdr_free(void* ptr)
        {
        }

        /**
         * hdr_histogram.c
         * Written by Michael Barker and released to the public domain,
         * as explained at http://creativecommons.org/publicdomain/zero/1.0/
         */

        /*  ######   #######  ##     ## ##    ## ########  ######  */
        /* ##    ## ##     ## ##     ## ###   ##    ##    ##    ## */
        /* ##       ##     ## ##     ## ####  ##    ##    ##       */
        /* ##       ##     ## ##     ## ## ## ##    ##     ######  */
        /* ##       ##     ## ##     ## ##  ####    ##          ## */
        /* ##    ## ##     ## ##     ## ##   ###    ##    ##    ## */
        /*  ######   #######   #######  ##    ##    ##     ######  */

        static s32 normalize_index(const hdr_histogram* h, s32 index)
        {
            s32 normalized_index;
            s32 adjustment = 0;
            if (h->normalizing_index_offset == 0)
            {
                return index;
            }

            normalized_index = index - h->normalizing_index_offset;

            if (normalized_index < 0)
            {
                adjustment = h->counts_len;
            }
            else if (normalized_index >= h->counts_len)
            {
                adjustment = -h->counts_len;
            }

            return normalized_index + adjustment;
        }

        static s64 counts_get_direct(const hdr_histogram* h, s32 index) { return h->counts[index]; }

        static s64 counts_get_normalised(const hdr_histogram* h, s32 index) { return counts_get_direct(h, normalize_index(h, index)); }

        static void counts_inc_normalised(hdr_histogram* h, s32 index, s64 value)
        {
            s32 normalised_index = normalize_index(h, index);
            h->counts[normalised_index] += value;
            h->total_count += value;
        }

        static void update_min_max(hdr_histogram* h, s64 value)
        {
            h->min_value = (value < h->min_value && value != 0) ? value : h->min_value;
            h->max_value = (value > h->max_value) ? value : h->max_value;
        }

        /* ##     ## ######## #### ##       #### ######## ##    ## */
        /* ##     ##    ##     ##  ##        ##     ##     ##  ##  */
        /* ##     ##    ##     ##  ##        ##     ##      ####   */
        /* ##     ##    ##     ##  ##        ##     ##       ##    */
        /* ##     ##    ##     ##  ##        ##     ##       ##    */
        /* ##     ##    ##     ##  ##        ##     ##       ##    */
        /*  #######     ##    #### ######## ####    ##       ##    */

        static s64 power(s64 base, s64 exp)
        {
            s64 result = 1;
            while (exp)
            {
                result *= base;
                exp--;
            }
            return result;
        }

#if defined(_MSC_VER) && !(defined(__clang__) && (defined(_M_ARM) || defined(_M_ARM64)))
#    if defined(_WIN64)
#        pragma intrinsic(_BitScanReverse64)
#    else
#        pragma intrinsic(_BitScanReverse)
#    endif
#endif

        static s32 count_leading_zeros_64(s64 value)
        {
#if defined(_MSC_VER) && !(defined(__clang__) && (defined(_M_ARM) || defined(_M_ARM64)))
            uint32_t leading_zero = 0;
#    if defined(_WIN64)
            _BitScanReverse64(&leading_zero, value);
#    else
            uint32_t high = value >> 32;
            if (_BitScanReverse(&leading_zero, high))
            {
                leading_zero += 32;
            }
            else
            {
                uint32_t low = value & 0x00000000FFFFFFFF;
                _BitScanReverse(&leading_zero, low);
            }
#    endif
            return 63 - leading_zero; /* smallest power of 2 containing value */
#else
            return __builtin_clzll(value); /* smallest power of 2 containing value */
#endif
        }

        static s32 get_bucket_index(const hdr_histogram* h, s64 value)
        {
            s32 pow2ceiling = 64 - count_leading_zeros_64(value | h->sub_bucket_mask); /* smallest power of 2 containing value */
            return pow2ceiling - h->unit_magnitude - (h->sub_bucket_half_count_magnitude + 1);
        }

        static s32 get_sub_bucket_index(s64 value, s32 bucket_index, s32 unit_magnitude) { return (s32)(value >> (bucket_index + unit_magnitude)); }

        static s32 counts_index(const hdr_histogram* h, s32 bucket_index, s32 sub_bucket_index)
        {
            /* Calculate the index for the first entry in the bucket: */
            /* (The following is the equivalent of ((bucket_index + 1) * subBucketHalfCount) ): */
            s32 bucket_base_index = (bucket_index + 1) << h->sub_bucket_half_count_magnitude;
            /* Calculate the offset in the bucket: */
            s32 offset_in_bucket = sub_bucket_index - h->sub_bucket_half_count;
            /* The following is the equivalent of ((sub_bucket_index  - subBucketHalfCount) + bucketBaseIndex; */
            return bucket_base_index + offset_in_bucket;
        }

        static s64 value_from_index(s32 bucket_index, s32 sub_bucket_index, s32 unit_magnitude) { return ((s64)sub_bucket_index) << (bucket_index + unit_magnitude); }

        s32 counts_index_for(const hdr_histogram* h, s64 value)
        {
            s32 bucket_index     = get_bucket_index(h, value);
            s32 sub_bucket_index = get_sub_bucket_index(value, bucket_index, h->unit_magnitude);

            return counts_index(h, bucket_index, sub_bucket_index);
        }

        s64 hdr_value_at_index(const hdr_histogram* h, s32 index)
        {
            s32 bucket_index     = (index >> h->sub_bucket_half_count_magnitude) - 1;
            s32 sub_bucket_index = (index & (h->sub_bucket_half_count - 1)) + h->sub_bucket_half_count;

            if (bucket_index < 0)
            {
                sub_bucket_index -= h->sub_bucket_half_count;
                bucket_index = 0;
            }

            return value_from_index(bucket_index, sub_bucket_index, h->unit_magnitude);
        }

        s64 hdr_size_of_equivalent_value_range(const hdr_histogram* h, s64 value)
        {
            s32 bucket_index     = get_bucket_index(h, value);
            s32 sub_bucket_index = get_sub_bucket_index(value, bucket_index, h->unit_magnitude);
            s32 adjusted_bucket  = (sub_bucket_index >= h->sub_bucket_count) ? (bucket_index + 1) : bucket_index;
            return (s64)(1) << (h->unit_magnitude + adjusted_bucket);
        }

        static s64 size_of_equivalent_value_range_given_bucket_indices(const hdr_histogram* h, s32 bucket_index, s32 sub_bucket_index)
        {
            const s32 adjusted_bucket = (sub_bucket_index >= h->sub_bucket_count) ? (bucket_index + 1) : bucket_index;
            return (s64)(1) << (h->unit_magnitude + adjusted_bucket);
        }

        static s64 lowest_equivalent_value(const hdr_histogram* h, s64 value)
        {
            s32 bucket_index     = get_bucket_index(h, value);
            s32 sub_bucket_index = get_sub_bucket_index(value, bucket_index, h->unit_magnitude);
            return value_from_index(bucket_index, sub_bucket_index, h->unit_magnitude);
        }

        static s64 lowest_equivalent_value_given_bucket_indices(const hdr_histogram* h, s32 bucket_index, s32 sub_bucket_index) { return value_from_index(bucket_index, sub_bucket_index, h->unit_magnitude); }

        s64 hdr_next_non_equivalent_value(const hdr_histogram* h, s64 value) { return lowest_equivalent_value(h, value) + hdr_size_of_equivalent_value_range(h, value); }

        static s64 highest_equivalent_value(const hdr_histogram* h, s64 value) { return hdr_next_non_equivalent_value(h, value) - 1; }

        s64 hdr_median_equivalent_value(const hdr_histogram* h, s64 value) { return lowest_equivalent_value(h, value) + (hdr_size_of_equivalent_value_range(h, value) >> 1); }

        static s64 non_zero_min(const hdr_histogram* h)
        {
            if (limits_t<s64>::maximum() == h->min_value)
            {
                return limits_t<s64>::maximum();
            }

            return lowest_equivalent_value(h, h->min_value);
        }

        void hdr_reset_internal_counters(hdr_histogram* h)
        {
            s32 min_non_zero_index   = -1;
            s32 max_index            = -1;
            s64 observed_total_count = 0;
            s32 i;

            for (i = 0; i < h->counts_len; i++)
            {
                s64 count_at_index;

                if ((count_at_index = counts_get_direct(h, i)) > 0)
                {
                    observed_total_count += count_at_index;
                    max_index = i;
                    if (min_non_zero_index == -1 && i != 0)
                    {
                        min_non_zero_index = i;
                    }
                }
            }

            if (max_index == -1)
            {
                h->max_value = 0;
            }
            else
            {
                s64 max_value = hdr_value_at_index(h, max_index);
                h->max_value  = highest_equivalent_value(h, max_value);
            }

            if (min_non_zero_index == -1)
            {
                h->min_value = limits_t<s64>::maximum();
            }
            else
            {
                h->min_value = hdr_value_at_index(h, min_non_zero_index);
            }

            h->total_count = observed_total_count;
        }

        static s32 buckets_needed_to_cover_value(s64 value, s32 sub_bucket_count, s32 unit_magnitude)
        {
            s64 smallest_untrackable_value = ((s64)sub_bucket_count) << unit_magnitude;
            s32 buckets_needed             = 1;
            while (smallest_untrackable_value <= value)
            {
                if (smallest_untrackable_value > limits_t<s64>::maximum() / 2)
                {
                    return buckets_needed + 1;
                }
                smallest_untrackable_value <<= 1;
                buckets_needed++;
            }

            return buckets_needed;
        }

        /* ##     ## ######## ##     ##  #######  ########  ##    ## */
        /* ###   ### ##       ###   ### ##     ## ##     ##  ##  ##  */
        /* #### #### ##       #### #### ##     ## ##     ##   ####   */
        /* ## ### ## ######   ## ### ## ##     ## ########     ##    */
        /* ##     ## ##       ##     ## ##     ## ##   ##      ##    */
        /* ##     ## ##       ##     ## ##     ## ##    ##     ##    */
        /* ##     ## ######## ##     ##  #######  ##     ##    ##    */

        s32 hdr_calculate_bucket_config(s64 lowest_discernible_value, s64 highest_trackable_value, s32 significant_figures, struct hdr_histogram_bucket_config* cfg)
        {
            s32 sub_bucket_count_magnitude;
            s64 largest_value_with_single_unit_resolution;

            if (lowest_discernible_value < 1 || significant_figures < 1 || 5 < significant_figures || lowest_discernible_value * 2 > highest_trackable_value)
            {
                return EINVAL;
            }

            cfg->lowest_discernible_value = lowest_discernible_value;
            cfg->significant_figures      = significant_figures;
            cfg->highest_trackable_value  = highest_trackable_value;

            largest_value_with_single_unit_resolution = 2 * power(10, significant_figures);
            sub_bucket_count_magnitude                = (s32)ceil(log((f64)largest_value_with_single_unit_resolution) / log(2));
            cfg->sub_bucket_half_count_magnitude      = ((sub_bucket_count_magnitude > 1) ? sub_bucket_count_magnitude : 1) - 1;

            f64 unit_magnitude = log((f64)lowest_discernible_value) / log(2);
            if (limits_t<s32>::maximum() < unit_magnitude)
            {
                return EINVAL;
            }

            cfg->unit_magnitude        = (s32)unit_magnitude;
            cfg->sub_bucket_count      = (s32)pow(2, (cfg->sub_bucket_half_count_magnitude + 1));
            cfg->sub_bucket_half_count = cfg->sub_bucket_count / 2;
            cfg->sub_bucket_mask       = ((s64)cfg->sub_bucket_count - 1) << cfg->unit_magnitude;

            if (cfg->unit_magnitude + cfg->sub_bucket_half_count_magnitude > 61)
            {
                return EINVAL;
            }

            cfg->bucket_count = buckets_needed_to_cover_value(highest_trackable_value, cfg->sub_bucket_count, (s32)cfg->unit_magnitude);
            cfg->counts_len   = (cfg->bucket_count + 1) * (cfg->sub_bucket_count / 2);

            return 0;
        }

        void hdr_init_preallocated(hdr_histogram* h, struct hdr_histogram_bucket_config* cfg)
        {
            h->lowest_discernible_value        = cfg->lowest_discernible_value;
            h->highest_trackable_value         = cfg->highest_trackable_value;
            h->unit_magnitude                  = (s32)cfg->unit_magnitude;
            h->significant_figures             = (s32)cfg->significant_figures;
            h->sub_bucket_half_count_magnitude = cfg->sub_bucket_half_count_magnitude;
            h->sub_bucket_half_count           = cfg->sub_bucket_half_count;
            h->sub_bucket_mask                 = cfg->sub_bucket_mask;
            h->sub_bucket_count                = cfg->sub_bucket_count;
            h->min_value                       = limits_t<s64>::maximum();
            h->max_value                       = 0;
            h->normalizing_index_offset        = 0;
            h->conversion_ratio                = 1.0;
            h->bucket_count                    = cfg->bucket_count;
            h->counts_len                      = cfg->counts_len;
            h->total_count                     = 0;
        }

        s32 hdr_init(s64 lowest_discernible_value, s64 highest_trackable_value, s32 significant_figures, hdr_histogram** result)
        {
            s64*                               counts;
            struct hdr_histogram_bucket_config cfg;
            hdr_histogram*                     histogram;

            s32 r = hdr_calculate_bucket_config(lowest_discernible_value, highest_trackable_value, significant_figures, &cfg);
            if (r)
            {
                return r;
            }

            counts = (s64*)hdr_calloc((u64)cfg.counts_len, sizeof(s64));
            if (!counts)
            {
                return ENOMEM;
            }

            histogram = (hdr_histogram*)hdr_calloc(1, sizeof(struct hdr_histogram));
            if (!histogram)
            {
                hdr_free(counts);
                return ENOMEM;
            }

            histogram->counts = counts;

            hdr_init_preallocated(histogram, &cfg);
            *result = histogram;

            return 0;
        }

        void hdr_close(hdr_histogram* h)
        {
            if (h)
            {
                hdr_free(h->counts);
                hdr_free(h);
            }
        }

        s32 hdr_alloc(s64 highest_trackable_value, s32 significant_figures, hdr_histogram** result) { return hdr_init(1, highest_trackable_value, significant_figures, result); }

        /* reset a histogram to zero. */
        void hdr_reset(hdr_histogram* h)
        {
            h->total_count = 0;
            h->min_value   = limits_t<s64>::maximum();
            h->max_value   = 0;
            nmem::memset(h->counts, 0, (sizeof(s64) * h->counts_len));
        }

        u64 hdr_get_memory_size(hdr_histogram* h) { return (u64)sizeof(struct hdr_histogram) + (u64)h->counts_len * (u64)sizeof(s64); }

        /* ##     ## ########  ########     ###    ######## ########  ######  */
        /* ##     ## ##     ## ##     ##   ## ##      ##    ##       ##    ## */
        /* ##     ## ##     ## ##     ##  ##   ##     ##    ##       ##       */
        /* ##     ## ########  ##     ## ##     ##    ##    ######    ######  */
        /* ##     ## ##        ##     ## #########    ##    ##             ## */
        /* ##     ## ##        ##     ## ##     ##    ##    ##       ##    ## */
        /*  #######  ##        ########  ##     ##    ##    ########  ######  */

        bool hdr_record_value(hdr_histogram* h, s64 value) { return hdr_record_values(h, value, 1); }

        bool hdr_record_values(hdr_histogram* h, s64 value, s64 count)
        {
            s32 counts_index;

            if (value < 0)
            {
                return false;
            }

            counts_index = counts_index_for(h, value);

            if (counts_index < 0 || h->counts_len <= counts_index)
            {
                return false;
            }

            counts_inc_normalised(h, counts_index, count);
            update_min_max(h, value);

            return true;
        }

        bool hdr_record_corrected_value(hdr_histogram* h, s64 value, s64 expected_interval) { return hdr_record_corrected_values(h, value, 1, expected_interval); }

        bool hdr_record_corrected_values(hdr_histogram* h, s64 value, s64 count, s64 expected_interval)
        {
            s64 missing_value;

            if (!hdr_record_values(h, value, count))
            {
                return false;
            }

            if (expected_interval <= 0 || value <= expected_interval)
            {
                return true;
            }

            missing_value = value - expected_interval;
            for (; missing_value >= expected_interval; missing_value -= expected_interval)
            {
                if (!hdr_record_values(h, missing_value, count))
                {
                    return false;
                }
            }

            return true;
        }

        s64 hdr_add(hdr_histogram* h, const hdr_histogram* from)
        {
            struct hdr_iter iter;
            s64             dropped = 0;
            hdr_iter_recorded_init(&iter, from);

            while (hdr_iter_next(&iter))
            {
                s64 value = iter.value;
                s64 count = iter.count;

                if (!hdr_record_values(h, value, count))
                {
                    dropped += count;
                }
            }

            return dropped;
        }

        s64 hdr_add_while_correcting_for_coordinated_omission(hdr_histogram* h, hdr_histogram* from, s64 expected_interval)
        {
            struct hdr_iter iter;
            s64             dropped = 0;
            hdr_iter_recorded_init(&iter, from);

            while (hdr_iter_next(&iter))
            {
                s64 value = iter.value;
                s64 count = iter.count;

                if (!hdr_record_corrected_values(h, value, count, expected_interval))
                {
                    dropped += count;
                }
            }

            return dropped;
        }

        /* ##     ##    ###    ##       ##     ## ########  ######  */
        /* ##     ##   ## ##   ##       ##     ## ##       ##    ## */
        /* ##     ##  ##   ##  ##       ##     ## ##       ##       */
        /* ##     ## ##     ## ##       ##     ## ######    ######  */
        /*  ##   ##  ######### ##       ##     ## ##             ## */
        /*   ## ##   ##     ## ##       ##     ## ##       ##    ## */
        /*    ###    ##     ## ########  #######  ########  ######  */

        s64 hdr_max(const hdr_histogram* h)
        {
            if (0 == h->max_value)
            {
                return 0;
            }

            return highest_equivalent_value(h, h->max_value);
        }

        s64 hdr_min(const hdr_histogram* h)
        {
            if (0 < hdr_count_at_index(h, 0))
            {
                return 0;
            }

            return non_zero_min(h);
        }

        static s64 get_value_from_idx_up_to_count(const hdr_histogram* h, s64 count_at_percentile)
        {
            s64 count_to_idx = 0;

            count_at_percentile = 0 < count_at_percentile ? count_at_percentile : 1;
            for (s32 idx = 0; idx < h->counts_len; idx++)
            {
                count_to_idx += h->counts[idx];
                if (count_to_idx >= count_at_percentile)
                {
                    return hdr_value_at_index(h, idx);
                }
            }

            return 0;
        }

        s64 hdr_value_at_percentile(const hdr_histogram* h, f64 percentile)
        {
            f64 requested_percentile = percentile < 100.0 ? percentile : 100.0;
            s64 count_at_percentile  = (s64)(((requested_percentile / 100) * h->total_count) + 0.5);
            s64 value_from_idx       = get_value_from_idx_up_to_count(h, count_at_percentile);
            if (percentile == 0.0)
            {
                return lowest_equivalent_value(h, value_from_idx);
            }
            return highest_equivalent_value(h, value_from_idx);
        }

        s32 hdr_value_at_percentiles(const hdr_histogram* h, const f64* percentiles, s64* values, u64 length)
        {
            if (nullptr == percentiles || nullptr == values)
            {
                return EINVAL;
            }

            hdr_iter  iter;
            const s64 total_count = h->total_count;
            // to avoid allocations we use the values array for intermediate computation
            // i.e. to store the expected cumulative count at each percentile
            for (u64 i = 0; i < length; i++)
            {
                const f64 requested_percentile = percentiles[i] < 100.0 ? percentiles[i] : 100.0;
                const s64 count_at_percentile  = (s64)(((requested_percentile / 100) * total_count) + 0.5);
                values[i]                      = count_at_percentile > 1 ? count_at_percentile : 1;
            }

            hdr_iter_init(&iter, h);
            s64 total  = 0;
            u64 at_pos = 0;
            while (hdr_iter_next(&iter) && at_pos < length)
            {
                total += iter.count;
                while (at_pos < length && total >= values[at_pos])
                {
                    values[at_pos] = highest_equivalent_value(h, iter.value);
                    at_pos++;
                }
            }
            return 0;
        }

        f64 hdr_mean(const hdr_histogram* h)
        {
            struct hdr_iter iter;
            s64             total = 0, count = 0;
            s64             total_count = h->total_count;

            hdr_iter_init(&iter, h);

            while (hdr_iter_next(&iter) && count < total_count)
            {
                if (0 != iter.count)
                {
                    count += iter.count;
                    total += iter.count * hdr_median_equivalent_value(h, iter.value);
                }
            }

            return (total * 1.0) / total_count;
        }

        f64 hdr_stddev(const hdr_histogram* h)
        {
            f64 mean                = hdr_mean(h);
            f64 geometric_dev_total = 0.0;

            struct hdr_iter iter;
            hdr_iter_init(&iter, h);

            while (hdr_iter_next(&iter))
            {
                if (0 != iter.count)
                {
                    f64 dev = (hdr_median_equivalent_value(h, iter.value) * 1.0) - mean;
                    geometric_dev_total += (dev * dev) * iter.count;
                }
            }

            return sqrt(geometric_dev_total / h->total_count);
        }

        bool hdr_values_are_equivalent(const hdr_histogram* h, s64 a, s64 b) { return lowest_equivalent_value(h, a) == lowest_equivalent_value(h, b); }

        s64 hdr_lowest_equivalent_value(const hdr_histogram* h, s64 value) { return lowest_equivalent_value(h, value); }

        s64 hdr_count_at_value(const hdr_histogram* h, s64 value) { return counts_get_normalised(h, counts_index_for(h, value)); }

        s64 hdr_count_at_index(const hdr_histogram* h, s32 index) { return counts_get_normalised(h, index); }

        /* #### ######## ######## ########     ###    ########  #######  ########   ######  */
        /*  ##     ##    ##       ##     ##   ## ##      ##    ##     ## ##     ## ##    ## */
        /*  ##     ##    ##       ##     ##  ##   ##     ##    ##     ## ##     ## ##       */
        /*  ##     ##    ######   ########  ##     ##    ##    ##     ## ########   ######  */
        /*  ##     ##    ##       ##   ##   #########    ##    ##     ## ##   ##         ## */
        /*  ##     ##    ##       ##    ##  ##     ##    ##    ##     ## ##    ##  ##    ## */
        /* ####    ##    ######## ##     ## ##     ##    ##     #######  ##     ##  ######  */

        static bool has_buckets(hdr_iter* iter) { return iter->counts_index < iter->h->counts_len; }

        static bool has_next(hdr_iter* iter) { return iter->cumulative_count < iter->total_count; }

        static bool move_next(hdr_iter* iter)
        {
            iter->counts_index++;

            if (!has_buckets(iter))
            {
                return false;
            }

            iter->count = counts_get_normalised(iter->h, iter->counts_index);
            iter->cumulative_count += iter->count;
            const s64 value                          = hdr_value_at_index(iter->h, iter->counts_index);
            const s32 bucket_index                   = get_bucket_index(iter->h, value);
            const s32 sub_bucket_index               = get_sub_bucket_index(value, bucket_index, iter->h->unit_magnitude);
            const s64 leq                            = lowest_equivalent_value_given_bucket_indices(iter->h, bucket_index, sub_bucket_index);
            const s64 size_of_equivalent_value_range = size_of_equivalent_value_range_given_bucket_indices(iter->h, bucket_index, sub_bucket_index);
            iter->lowest_equivalent_value            = leq;
            iter->value                              = value;
            iter->highest_equivalent_value           = leq + size_of_equivalent_value_range - 1;
            iter->median_equivalent_value            = leq + (size_of_equivalent_value_range >> 1);

            return true;
        }

        static s64 peek_next_value_from_index(hdr_iter* iter) { return hdr_value_at_index(iter->h, iter->counts_index + 1); }

        static bool next_value_greater_than_reporting_level_upper_bound(hdr_iter* iter, s64 reporting_level_upper_bound)
        {
            if (iter->counts_index >= iter->h->counts_len)
            {
                return false;
            }

            return peek_next_value_from_index(iter) > reporting_level_upper_bound;
        }

        static bool basic_iter_next(hdr_iter* iter)
        {
            if (!has_next(iter) || iter->counts_index >= iter->h->counts_len)
            {
                return false;
            }

            move_next(iter);

            return true;
        }

        static void update_iterated_values(hdr_iter* iter, s64 new_value_iterated_to)
        {
            iter->value_iterated_from = iter->value_iterated_to;
            iter->value_iterated_to   = new_value_iterated_to;
        }

        static bool all_values_iter_next(hdr_iter* iter)
        {
            bool result = move_next(iter);

            if (result)
            {
                update_iterated_values(iter, iter->value);
            }

            return result;
        }

        void hdr_iter_init(hdr_iter* iter, const hdr_histogram* h)
        {
            iter->h = h;

            iter->counts_index             = -1;
            iter->total_count              = h->total_count;
            iter->count                    = 0;
            iter->cumulative_count         = 0;
            iter->value                    = 0;
            iter->highest_equivalent_value = 0;
            iter->value_iterated_from      = 0;
            iter->value_iterated_to        = 0;

            iter->_next_fp = all_values_iter_next;
        }

        bool hdr_iter_next(hdr_iter* iter) { return iter->_next_fp(iter); }

        /* ########  ######## ########   ######  ######## ##    ## ######## #### ##       ########  ######  */
        /* ##     ## ##       ##     ## ##    ## ##       ###   ##    ##     ##  ##       ##       ##    ## */
        /* ##     ## ##       ##     ## ##       ##       ####  ##    ##     ##  ##       ##       ##       */
        /* ########  ######   ########  ##       ######   ## ## ##    ##     ##  ##       ######    ######  */
        /* ##        ##       ##   ##   ##       ##       ##  ####    ##     ##  ##       ##             ## */
        /* ##        ##       ##    ##  ##    ## ##       ##   ###    ##     ##  ##       ##       ##    ## */
        /* ##        ######## ##     ##  ######  ######## ##    ##    ##    #### ######## ########  ######  */

        static bool percentile_iter_next(hdr_iter* iter)
        {
            s64 temp, half_distance, percentile_reporting_ticks;

            struct hdr_iter_percentiles* percentiles = &iter->specifics.percentiles;

            if (!has_next(iter))
            {
                if (percentiles->seen_last_value)
                {
                    return false;
                }

                percentiles->seen_last_value = true;
                percentiles->percentile      = 100.0;

                return true;
            }

            if (iter->counts_index == -1 && !basic_iter_next(iter))
            {
                return false;
            }

            do
            {
                f64 current_percentile = (100.0 * (f64)iter->cumulative_count) / iter->h->total_count;
                if (iter->count != 0 && percentiles->percentile_to_iterate_to <= current_percentile)
                {
                    update_iterated_values(iter, highest_equivalent_value(iter->h, iter->value));

                    percentiles->percentile    = percentiles->percentile_to_iterate_to;
                    temp                       = (s64)(log(100 / (100.0 - (percentiles->percentile_to_iterate_to))) / log(2)) + 1;
                    half_distance              = (s64)pow(2, (f64)temp);
                    percentile_reporting_ticks = percentiles->ticks_per_half_distance * half_distance;
                    percentiles->percentile_to_iterate_to += 100.0 / percentile_reporting_ticks;

                    return true;
                }
            } while (basic_iter_next(iter));

            return true;
        }

        void hdr_iter_percentile_init(hdr_iter* iter, const hdr_histogram* h, s32 ticks_per_half_distance)
        {
            iter->h = h;

            hdr_iter_init(iter, h);

            iter->specifics.percentiles.seen_last_value          = false;
            iter->specifics.percentiles.ticks_per_half_distance  = ticks_per_half_distance;
            iter->specifics.percentiles.percentile_to_iterate_to = 0.0;
            iter->specifics.percentiles.percentile               = 0.0;

            iter->_next_fp = percentile_iter_next;
        }

        static void format_line_string(char* str, u64 len, s32 significant_figures, format_type format)
        {
#if defined(_MSC_VER)
#    define snprintf _snprintf
#    pragma warning(push)
#    pragma warning(disable : 4996)
#endif
            const char* format_str = "%s%d%s";

            switch (format)
            {
                case CSV: snprintf(str, len, format_str, "%.", significant_figures, "f,%f,%d,%.2f\n"); break;
                case CLASSIC: snprintf(str, len, format_str, "%12.", significant_figures, "f %12f %12d %12.2f\n"); break;
                default: snprintf(str, len, format_str, "%12.", significant_figures, "f %12f %12d %12.2f\n");
            }
#if defined(_MSC_VER)
#    undef snprintf
#    pragma warning(pop)
#endif
        }

        /* ########  ########  ######   #######  ########  ########  ######## ########   */
        /* ##     ## ##       ##    ## ##     ## ##     ## ##     ## ##       ##     ##  */
        /* ##     ## ##       ##       ##     ## ##     ## ##     ## ##       ##     ##  */
        /* ########  ######   ##       ##     ## ########  ##     ## ######   ##     ##  */
        /* ##   ##   ##       ##       ##     ## ##   ##   ##     ## ##       ##     ##  */
        /* ##    ##  ##       ##    ## ##     ## ##    ##  ##     ## ##       ##     ##  */
        /* ##     ## ########  ######   #######  ##     ## ########  ######## ########   */

        static bool recorded_iter_next(hdr_iter* iter)
        {
            while (basic_iter_next(iter))
            {
                if (iter->count != 0)
                {
                    update_iterated_values(iter, iter->value);

                    iter->specifics.recorded.count_added_in_this_iteration_step = iter->count;
                    return true;
                }
            }

            return false;
        }

        void hdr_iter_recorded_init(hdr_iter* iter, const hdr_histogram* h)
        {
            hdr_iter_init(iter, h);

            iter->specifics.recorded.count_added_in_this_iteration_step = 0;

            iter->_next_fp = recorded_iter_next;
        }

        /* ##       #### ##    ## ########    ###    ########  */
        /* ##        ##  ###   ## ##         ## ##   ##     ## */
        /* ##        ##  ####  ## ##        ##   ##  ##     ## */
        /* ##        ##  ## ## ## ######   ##     ## ########  */
        /* ##        ##  ##  #### ##       ######### ##   ##   */
        /* ##        ##  ##   ### ##       ##     ## ##    ##  */
        /* ######## #### ##    ## ######## ##     ## ##     ## */

        static bool iter_linear_next(hdr_iter* iter)
        {
            struct hdr_iter_linear* linear = &iter->specifics.linear;

            linear->count_added_in_this_iteration_step = 0;

            if (has_next(iter) || next_value_greater_than_reporting_level_upper_bound(iter, linear->next_value_reporting_level_lowest_equivalent))
            {
                do
                {
                    if (iter->value >= linear->next_value_reporting_level_lowest_equivalent)
                    {
                        update_iterated_values(iter, linear->next_value_reporting_level);

                        linear->next_value_reporting_level += linear->value_units_per_bucket;
                        linear->next_value_reporting_level_lowest_equivalent = lowest_equivalent_value(iter->h, linear->next_value_reporting_level);

                        return true;
                    }

                    if (!move_next(iter))
                    {
                        return true;
                    }

                    linear->count_added_in_this_iteration_step += iter->count;
                } while (true);
            }

            return false;
        }

        void hdr_iter_linear_init(hdr_iter* iter, const hdr_histogram* h, s64 value_units_per_bucket)
        {
            hdr_iter_init(iter, h);

            iter->specifics.linear.count_added_in_this_iteration_step           = 0;
            iter->specifics.linear.value_units_per_bucket                       = value_units_per_bucket;
            iter->specifics.linear.next_value_reporting_level                   = value_units_per_bucket;
            iter->specifics.linear.next_value_reporting_level_lowest_equivalent = lowest_equivalent_value(h, value_units_per_bucket);

            iter->_next_fp = iter_linear_next;
        }

        /* ##        #######   ######      ###    ########  #### ######## ##     ## ##     ## ####  ######  */
        /* ##       ##     ## ##    ##    ## ##   ##     ##  ##     ##    ##     ## ###   ###  ##  ##    ## */
        /* ##       ##     ## ##         ##   ##  ##     ##  ##     ##    ##     ## #### ####  ##  ##       */
        /* ##       ##     ## ##   #### ##     ## ########   ##     ##    ######### ## ### ##  ##  ##       */
        /* ##       ##     ## ##    ##  ######### ##   ##    ##     ##    ##     ## ##     ##  ##  ##       */
        /* ##       ##     ## ##    ##  ##     ## ##    ##   ##     ##    ##     ## ##     ##  ##  ##    ## */
        /* ########  #######   ######   ##     ## ##     ## ####    ##    ##     ## ##     ## ####  ######  */

        static bool log_iter_next(hdr_iter* iter)
        {
            hdr_iter_log* logarithmic = &iter->specifics.log;

            logarithmic->count_added_in_this_iteration_step = 0;

            if (has_next(iter) || next_value_greater_than_reporting_level_upper_bound(iter, logarithmic->next_value_reporting_level_lowest_equivalent))
            {
                do
                {
                    if (iter->value >= logarithmic->next_value_reporting_level_lowest_equivalent)
                    {
                        update_iterated_values(iter, logarithmic->next_value_reporting_level);

                        logarithmic->next_value_reporting_level *= (s64)logarithmic->log_base;
                        logarithmic->next_value_reporting_level_lowest_equivalent = lowest_equivalent_value(iter->h, logarithmic->next_value_reporting_level);

                        return true;
                    }

                    if (!move_next(iter))
                    {
                        return true;
                    }

                    logarithmic->count_added_in_this_iteration_step += iter->count;
                } while (true);
            }

            return false;
        }

        void hdr_iter_log_init(hdr_iter* iter, const hdr_histogram* h, s64 value_units_first_bucket, f64 log_base)
        {
            hdr_iter_init(iter, h);
            iter->specifics.log.count_added_in_this_iteration_step           = 0;
            iter->specifics.log.log_base                                     = log_base;
            iter->specifics.log.next_value_reporting_level                   = value_units_first_bucket;
            iter->specifics.log.next_value_reporting_level_lowest_equivalent = lowest_equivalent_value(h, value_units_first_bucket);

            iter->_next_fp = log_iter_next;
        }

        /* Printing. */

        static const char* format_head_string(format_type format)
        {
            switch (format)
            {
                case CSV: return "%s,%s,%s,%s\n";
                case CLASSIC:
                default: return "%12s %12s %12s %12s\n\n";
            }
        }

        static const char CLASSIC_FOOTER[] = "#[Mean    = %12.3f, StdDeviation   = %12.3f]\n"
                                             "#[Max     = %12.3f, Total count    = %12" PRIu64 "]\n"
                                             "#[Buckets = %12d, SubBuckets     = %12d]\n";

        s32 hdr_percentiles_print(hdr_histogram* h, FILE* stream, s32 ticks_per_half_distance, f64 value_scale, format_type format)
        {
            char                         line_format[25];
            const char*                  head_format;
            s32                          rc = 0;
            struct hdr_iter              iter;
            struct hdr_iter_percentiles* percentiles;

            format_line_string(line_format, 25, h->significant_figures, format);
            head_format = format_head_string(format);

            hdr_iter_percentile_init(&iter, h, ticks_per_half_distance);

            if (fprintf(stream, head_format, "Value", "Percentile", "TotalCount", "1/(1-Percentile)") < 0)
            {
                rc = EIO;
                goto cleanup;
            }

            percentiles = &iter.specifics.percentiles;
            while (hdr_iter_next(&iter))
            {
                f64 value               = iter.highest_equivalent_value / value_scale;
                f64 percentile          = percentiles->percentile / 100.0;
                s64 total_count         = iter.cumulative_count;
                f64 inverted_percentile = (1.0 / (1.0 - percentile));

                if (fprintf(stream, line_format, value, percentile, total_count, inverted_percentile) < 0)
                {
                    rc = EIO;
                    goto cleanup;
                }
            }

            if (CLASSIC == format)
            {
                f64 mean   = hdr_mean(h) / value_scale;
                f64 stddev = hdr_stddev(h) / value_scale;
                f64 max    = hdr_max(h) / value_scale;

                if (fprintf(stream, CLASSIC_FOOTER, mean, stddev, max, h->total_count, h->bucket_count, h->sub_bucket_count) < 0)
                {
                    rc = EIO;
                    goto cleanup;
                }
            }

        cleanup:
            return rc;
        }
    } // namespace nhdr
};    // namespace ncore
