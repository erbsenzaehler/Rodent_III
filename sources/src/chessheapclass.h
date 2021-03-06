#include <cstdio>
#include <cstdlib>
#include <cstring>


class ChessHeapClass {
    static const int bucket_size_mb = 512;
    static const int max_memory_mb = 4096;
    static const int num_per_bucket = bucket_size_mb * 1024 * 1024 / sizeof(ENTRY);
    static const int arrays_size = max_memory_mb / bucket_size_mb;

    int    bucket_sizs[arrays_size];
    ENTRY *bucket_ptrs[arrays_size];

    void Free() {

        for (int i = 0; i < arrays_size && bucket_ptrs[i]; i++) {
            free(bucket_ptrs[i]);
            bucket_ptrs[i] = NULL;
        }
    }

  public:

    bool success;

    ChessHeapClass(): bucket_ptrs{}, success{false} {};

    bool Alloc(int size_mb) {

        if (size_mb > max_memory_mb)
            return false;

        Free();

        success = true;
        for (int i = 0; size_mb > 0 && success; i++) {
            bucket_sizs[i] = size_mb > bucket_size_mb ? bucket_size_mb : size_mb;
            bucket_ptrs[i] = (ENTRY *) malloc(1024 * 1024 * bucket_sizs[i]);
            success = bucket_ptrs[i] != NULL;
            size_mb -= bucket_size_mb;

            if (success)
                printf_debug("allocated: %dMB\n", bucket_sizs[i]);
        }

        if (!success)
            Free();

        return success;
    }

    void ZeroMem() {

        if (success)
            for (int i = 0; i < arrays_size && bucket_ptrs[i]; i++)
                memset(bucket_ptrs[i], 0, 1024 * 1024 * bucket_sizs[i]);
    }

    ENTRY *operator[](int entry_number) {

        const int num_of_bucket = entry_number / num_per_bucket;

        return bucket_ptrs[num_of_bucket] + entry_number - num_per_bucket * num_of_bucket;
    }

    ~ChessHeapClass() {

        Free();
    }
};
