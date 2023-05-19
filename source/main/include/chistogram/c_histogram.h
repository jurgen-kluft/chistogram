#ifndef __CHISTOGRAM_H__
#define __CHISTOGRAM_H__
#include "ccore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nhdr
    {
        // URL: https://github.com/HdrHistogram/HdrHistogram_c
        // License: https://github.com/HdrHistogram/HdrHistogram_c/blob/main/LICENSE.txt

        // Refactored mainly to control memory allocations and to make it easier to use by other packages.
        // This is not a complete port of the original C version.

        struct  hdr_histogram
        {
            s64  lowest_discernible_value;
            s64  highest_trackable_value;
            s32  unit_magnitude;
            s32  significant_figures;
            s32  sub_bucket_half_count_magnitude;
            s32  sub_bucket_half_count;
            s64  sub_bucket_mask;
            s32  sub_bucket_count;
            s32  bucket_count;
            s64  min_value;
            s64  max_value;
            f64  conversion_ratio;
            s32  normalizing_index_offset;
            s32  counts_len;
            s64  total_count;
            s64* counts;
        };

        /**
         * Allocate the memory and initialise the hdr_histogram.
         *
         * Due to the size of the histogram being the result of some reasonably
         * involved math on the input parameters this function it is tricky to stack allocate.
         * The histogram should be released with hdr_close
         *
         * @param lowest_discernible_value The smallest possible value that is distinguishable from 0.
         * Must be a positive integer that is >= 1. May be internally rounded down to nearest power of 2.
         * @param highest_trackable_value The largest possible value to be put into the
         * histogram.
         * @param significant_figures The level of precision for this histogram, i.e. the number
         * of figures in a decimal number that will be maintained.  E.g. a value of 3 will mean
         * the results from the histogram will be accurate up to the first three digits.  Must
         * be a value between 1 and 5 (inclusive).
         * @param result Output parameter to capture allocated histogram.
         * @return 0 on success, EINVAL if lowest_discernible_value is < 1 or the
         * significant_figure value is outside of the allowed range, ENOMEM if malloc
         * failed.
         */
        s32 hdr_init(s64 lowest_discernible_value, s64 highest_trackable_value, s32 significant_figures, hdr_histogram** result);

        /**
         * Free the memory and close the hdr_histogram.
         *
         * @param h The histogram you want to close.
         */
        void hdr_close(hdr_histogram* h);

        /**
         * Allocate the memory and initialise the hdr_histogram.  This is the equivalent of calling
         * hdr_init(1, highest_trackable_value, significant_figures, result);
         *
         * @deprecated use hdr_init.
         */
        s32 hdr_alloc(s64 highest_trackable_value, s32 significant_figures, hdr_histogram** result);

        /**
         * Reset a histogram to zero - empty out a histogram and re-initialise it
         *
         * If you want to re-use an existing histogram, but reset everything back to zero, this
         * is the routine to use.
         *
         * @param h The histogram you want to reset to empty.
         *
         */
        void hdr_reset(hdr_histogram* h);

        /**
         * Get the memory size of the hdr_histogram.
         *
         * @param h "This" pointer
         * @return The amount of memory used by the hdr_histogram in bytes
         */
        u64 hdr_get_memory_size(hdr_histogram* h);

        /**
         * Records a value in the histogram, will round this value of to a precision at or better
         * than the significant_figure specified at construction time.
         *
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @return false if the value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_value(hdr_histogram* h, s64 value);

        /**
         * Records a value in the histogram, will round this value of to a precision at or better
         * than the significant_figure specified at construction time.
         *
         * Will record this value atomically, however the whole structure may appear inconsistent
         * when read concurrently with this update.  Do NOT mix calls to this method with calls
         * to non-atomic updates.
         *
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @return false if the value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_value_atomic(hdr_histogram* h, s64 value);

        /**
         * Records count values in the histogram, will round this value of to a
         * precision at or better than the significant_figure specified at construction
         * time.
         *
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @param count Number of 'value's to add to the histogram
         * @return false if any value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_values(hdr_histogram* h, s64 value, s64 count);

        /**
         * Records count values in the histogram, will round this value of to a
         * precision at or better than the significant_figure specified at construction
         * time.
         *
         * Will record this value atomically, however the whole structure may appear inconsistent
         * when read concurrently with this update.  Do NOT mix calls to this method with calls
         * to non-atomic updates.
         *
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @param count Number of 'value's to add to the histogram
         * @return false if any value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_values_atomic(hdr_histogram* h, s64 value, s64 count);

        /**
         * Record a value in the histogram and backfill based on an expected interval.
         *
         * Records a value in the histogram, will round this value of to a precision at or better
         * than the significant_figure specified at construction time.  This is specifically used
         * for recording latency.  If the value is larger than the expected_interval then the
         * latency recording system has experienced co-ordinated omission.  This method fills in the
         * values that would have occurred had the client providing the load not been blocked.
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @param expected_interval The delay between recording values.
         * @return false if the value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_corrected_value(hdr_histogram* h, s64 value, s64 expected_interval);

        /**
         * Record a value in the histogram and backfill based on an expected interval.
         *
         * Records a value in the histogram, will round this value of to a precision at or better
         * than the significant_figure specified at construction time.  This is specifically used
         * for recording latency.  If the value is larger than the expected_interval then the
         * latency recording system has experienced co-ordinated omission.  This method fills in the
         * values that would have occurred had the client providing the load not been blocked.
         *
         * Will record this value atomically, however the whole structure may appear inconsistent
         * when read concurrently with this update.  Do NOT mix calls to this method with calls
         * to non-atomic updates.
         *
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @param expected_interval The delay between recording values.
         * @return false if the value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_corrected_value_atomic(hdr_histogram* h, s64 value, s64 expected_interval);

        /**
         * Record a value in the histogram 'count' times.  Applies the same correcting logic
         * as 'hdr_record_corrected_value'.
         *
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @param count Number of 'value's to add to the histogram
         * @param expected_interval The delay between recording values.
         * @return false if the value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_corrected_values(hdr_histogram* h, s64 value, s64 count, s64 expected_interval);

        /**
         * Record a value in the histogram 'count' times.  Applies the same correcting logic
         * as 'hdr_record_corrected_value'.
         *
         * Will record this value atomically, however the whole structure may appear inconsistent
         * when read concurrently with this update.  Do NOT mix calls to this method with calls
         * to non-atomic updates.
         *
         * @param h "This" pointer
         * @param value Value to add to the histogram
         * @param count Number of 'value's to add to the histogram
         * @param expected_interval The delay between recording values.
         * @return false if the value is larger than the highest_trackable_value and can't be recorded,
         * true otherwise.
         */
        bool hdr_record_corrected_values_atomic(hdr_histogram* h, s64 value, s64 count, s64 expected_interval);

        /**
         * Adds all of the values from 'from' to 'this' histogram.  Will return the
         * number of values that are dropped when copying.  Values will be dropped
         * if they around outside of h.lowest_discernible_value and
         * h.highest_trackable_value.
         *
         * @param h "This" pointer
         * @param from Histogram to copy values from.
         * @return The number of values dropped when copying.
         */
        s64 hdr_add(hdr_histogram* h, const hdr_histogram* from);

        /**
         * Adds all of the values from 'from' to 'this' histogram.  Will return the
         * number of values that are dropped when copying.  Values will be dropped
         * if they around outside of h.lowest_discernible_value and
         * h.highest_trackable_value.
         *
         * @param h "This" pointer
         * @param from Histogram to copy values from.
         * @return The number of values dropped when copying.
         */
        s64 hdr_add_while_correcting_for_coordinated_omission(hdr_histogram* h, hdr_histogram* from, s64 expected_interval);

        /**
         * Get minimum value from the histogram.  Will return 2^63-1 if the histogram
         * is empty.
         *
         * @param h "This" pointer
         */
        s64 hdr_min(const hdr_histogram* h);

        /**
         * Get maximum value from the histogram.  Will return 0 if the histogram
         * is empty.
         *
         * @param h "This" pointer
         */
        s64 hdr_max(const hdr_histogram* h);

        /**
         * Get the value at a specific percentile.
         *
         * @param h "This" pointer.
         * @param percentile The percentile to get the value for
         */
        s64 hdr_value_at_percentile(const hdr_histogram* h, f64 percentile);

        /**
         * Get the values at the given percentiles.
         *
         * @param h "This" pointer.
         * @param percentiles The ordered percentiles array to get the values for.
         * @param length Number of elements in the arrays.
         * @param values Destination array containing the values at the given percentiles.
         * The values array should be allocated by the caller.
         * @return 0 on success, ENOMEM if the provided destination array is null.
         */
        s32 hdr_value_at_percentiles(const hdr_histogram* h, const f64* percentiles, s64* values, u64 length);

        /**
         * Gets the standard deviation for the values in the histogram.
         *
         * @param h "This" pointer
         * @return The standard deviation
         */
        f64 hdr_stddev(const hdr_histogram* h);

        /**
         * Gets the mean for the values in the histogram.
         *
         * @param h "This" pointer
         * @return The mean
         */
        f64 hdr_mean(const hdr_histogram* h);

        /**
         * Determine if two values are equivalent with the histogram's resolution.
         * Where "equivalent" means that value samples recorded for any two
         * equivalent values are counted in a common total count.
         *
         * @param h "This" pointer
         * @param a first value to compare
         * @param b second value to compare
         * @return 'true' if values are equivalent with the histogram's resolution.
         */
        bool hdr_values_are_equivalent(const hdr_histogram* h, s64 a, s64 b);

        /**
         * Get the lowest value that is equivalent to the given value within the histogram's resolution.
         * Where "equivalent" means that value samples recorded for any two
         * equivalent values are counted in a common total count.
         *
         * @param h "This" pointer
         * @param value The given value
         * @return The lowest value that is equivalent to the given value within the histogram's resolution.
         */
        s64 hdr_lowest_equivalent_value(const hdr_histogram* h, s64 value);

        /**
         * Get the count of recorded values at a specific value
         * (to within the histogram resolution at the value level).
         *
         * @param h "This" pointer
         * @param value The value for which to provide the recorded count
         * @return The total count of values recorded in the histogram within the value range that is
         * {@literal >=} lowestEquivalentValue(<i>value</i>) and {@literal <=} highestEquivalentValue(<i>value</i>)
         */
        s64 hdr_count_at_value(const hdr_histogram* h, s64 value);
        s64 hdr_count_at_index(const hdr_histogram* h, s32 index);
        s64 hdr_value_at_index(const hdr_histogram* h, s32 index);

        struct hdr_iter_percentiles
        {
            bool seen_last_value;
            s32  ticks_per_half_distance;
            f64  percentile_to_iterate_to;
            f64  percentile;
        };

        struct hdr_iter_recorded
        {
            s64 count_added_in_this_iteration_step;
        };

        struct hdr_iter_linear
        {
            s64 value_units_per_bucket;
            s64 count_added_in_this_iteration_step;
            s64 next_value_reporting_level;
            s64 next_value_reporting_level_lowest_equivalent;
        };

        struct hdr_iter_log
        {
            f64 log_base;
            s64 count_added_in_this_iteration_step;
            s64 next_value_reporting_level;
            s64 next_value_reporting_level_lowest_equivalent;
        };

        /**
         * The basic iterator.  This is a generic structure
         * that supports all of the types of iteration.  Use
         * the appropriate initialiser to get the desired
         * iteration.
         *
         * @
         */
        struct hdr_iter
        {
            const hdr_histogram* h;
            /** raw index into the counts array */
            s32 counts_index;
            /** snapshot of the length at the time the iterator is created */
            s64 total_count;
            /** value directly from array for the current counts_index */
            s64 count;
            /** sum of all of the counts up to and including the count at this index */
            s64 cumulative_count;
            /** The current value based on counts_index */
            s64 value;
            s64 highest_equivalent_value;
            s64 lowest_equivalent_value;
            s64 median_equivalent_value;
            s64 value_iterated_from;
            s64 value_iterated_to;

            union
            {
                struct hdr_iter_percentiles percentiles;
                struct hdr_iter_recorded    recorded;
                struct hdr_iter_linear      linear;
                struct hdr_iter_log         log;
            } specifics;

            bool (*_next_fp)(struct hdr_iter* iter);
        };

        /**
         * Initalises the basic iterator.
         *
         * @param itr 'This' pointer
         * @param h The histogram to iterate over
         */
        void hdr_iter_init(struct hdr_iter* iter, const hdr_histogram* h);

        /**
         * Initialise the iterator for use with percentiles.
         */
        void hdr_iter_percentile_init(struct hdr_iter* iter, const hdr_histogram* h, s32 ticks_per_half_distance);

        /**
         * Initialise the iterator for use with recorded values.
         */
        void hdr_iter_recorded_init(struct hdr_iter* iter, const hdr_histogram* h);

        /**
         * Initialise the iterator for use with linear values.
         */
        void hdr_iter_linear_init(struct hdr_iter* iter, const hdr_histogram* h, s64 value_units_per_bucket);

        /**
         * Initialise the iterator for use with logarithmic values
         */
        void hdr_iter_log_init(struct hdr_iter* iter, const hdr_histogram* h, s64 value_units_first_bucket, f64 log_base);

        /**
         * Iterate to the next value for the iterator.  If there are no more values
         * available return faluse.
         *
         * @param itr 'This' pointer
         * @return 'false' if there are no values remaining for this iterator.
         */
        bool hdr_iter_next(struct hdr_iter* iter);

        typedef enum
        {
            CLASSIC,
            CSV
        } format_type;

        /**
         * Print out a percentile based histogram to the supplied stream.  Note that
         * this call will not flush the FILE, this is left up to the user.
         *
         * @param h 'This' pointer
         * @param stream The FILE to write the output to
         * @param ticks_per_half_distance The number of iteration steps per half-distance to 100%
         * @param value_scale Scale the output values by this amount
         * @param format_type Format to use, e.g. CSV.
         * @return 0 on success, error code on failure.  EIO if an error occurs writing
         * the output.
         */
        s32 hdr_percentiles_print(hdr_histogram* h, FILE* stream, s32 ticks_per_half_distance, f64 value_scale, format_type format);

        /**
         * Internal allocation methods, used by hdr_dbl_histogram.
         */
        struct hdr_histogram_bucket_config
        {
            s64 lowest_discernible_value;
            s64 highest_trackable_value;
            s64 unit_magnitude;
            s64 significant_figures;
            s32 sub_bucket_half_count_magnitude;
            s32 sub_bucket_half_count;
            s64 sub_bucket_mask;
            s32 sub_bucket_count;
            s32 bucket_count;
            s32 counts_len;
        };

        s32  hdr_calculate_bucket_config(s64 lowest_discernible_value, s64 highest_trackable_value, s32 significant_figures, struct hdr_histogram_bucket_config* cfg);
        void hdr_init_preallocated(hdr_histogram* h, struct hdr_histogram_bucket_config* cfg);
        s64  hdr_size_of_equivalent_value_range(const hdr_histogram* h, s64 value);
        s64  hdr_next_non_equivalent_value(const hdr_histogram* h, s64 value);
        s64  hdr_median_equivalent_value(const hdr_histogram* h, s64 value);

        /**
         * Used to reset counters after importing data manually into the histogram, used by the logging code
         * and other custom serialisation tools.
         */
        void hdr_reset_internal_counters(hdr_histogram* h);

    } // namespace nhg

}; // namespace ncore

#endif
