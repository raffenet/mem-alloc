#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <mpi.h>
#include <hip/hip_runtime_api.h>

#define BUFSIZE 1024

int main(int argc, char *argv[])
{
    int rocm_device_aware = 0;
    int len = 0, flag = 0;
    int *device_buf = NULL;
    MPI_File file;
    MPI_Status status;
    // usage mode: REQUESTED
    // supply mpi_memory_alloc_kinds to the MPI startup mechansim (not shown)
    MPI_Init(&argc, &argv);
    
    // usage mode: PROVIDED
    // Determine whether user has set the required
    // memory allocator kinds at startup. 
    MPI_Info_get_string(MPI_INFO_ENV, "mpi_memory_alloc_kinds",
			&len, NULL, &flag);
    if (flag) {
        char *val, *valptr, *kind;

        val = valptr = (char *) malloc(len);
        if (NULL == val) return 1;

        MPI_Info_get_string(MPI_INFO_ENV, "mpi_memory_alloc_kinds",
                            &len, val, &flag);

        while ((kind = strsep(&val, ",")) != NULL) {
            if (strcasecmp(kind, "rocm:device") == 0) {
                rocm_device_aware = 1;
            }
        }
        free(valptr);
    }

    hipMalloc((void**)&device_buf, BUFSIZE * sizeof(int));

    // The user could optionally create an info object,
    // set mpi_assert_memory_alloc_kind to the memory type
    // it plans to use, and pass this as an argument to
    // MPI_File_open. This approach has the potential to
    // enable further optimizations in the MPI library.
    MPI_File_open(MPI_COMM_WORLD, "inputfile",
		  MPI_MODE_RDONLY, MPI_INFO_NULL, &file);
    
    if (rocm_device_aware) {
        MPI_File_read(file, device_buf, BUFSIZE, MPI_INT, &status);
	printf("Using if part\n");
    }
    else {
        int *tmp_buf;
	printf("Using else part\n");
	tmp_buf = (int*) malloc (BUFSIZE * sizeof(int));
	MPI_File_read(file, tmp_buf, BUFSIZE, MPI_INT, &status);

	hipMemcpyAsync(device_buf, tmp_buf, BUFSIZE * sizeof(int),
		       hipMemcpyDefault, 0);
	hipStreamSynchronize(0);

	free (tmp_buf);
    }

    // launch compute kernel(s) 
    
    MPI_File_close(&file);
    hipFree(device_buf);

    MPI_Finalize();
    return 0;
}
