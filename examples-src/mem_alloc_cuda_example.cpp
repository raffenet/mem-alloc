#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <mpi.h>
#include <cuda_runtime.h>

int main(int argc, char *argv[])
{
    int cuda_device_aware = 0;
    int cuda_managed_aware = 0;
    int len = 0, flag = 0;
    int *managed_buf = NULL;
    int *device_buf = NULL, *system_buf = NULL;
    int nranks = 0;
    MPI_Info info;
    MPI_Session session;
    MPI_Group wgroup;
    MPI_Comm system_comm;
    MPI_Comm cuda_managed_comm = MPI_COMM_NULL;
    MPI_Comm cuda_device_comm = MPI_COMM_NULL;

    // Usage mode: REQUESTED
    MPI_Info_create(&info);
    MPI_Info_set(info, "mpi_memory_alloc_kinds",
                       "system,cuda:device,cuda:managed");
    MPI_Session_init(info, MPI_ERRORS_ARE_FATAL, &session);
    MPI_Info_free(&info);

    // Usage mode: PROVIDED
    MPI_Session_get_info(session, &info);
    MPI_Info_get_string(info, "mpi_memory_alloc_kinds",
                        &len, NULL, &flag);

    if (flag) {
        char *val, *valptr, *kind;

        val = valptr = (char *) malloc(len);
        if (NULL == val) return 1;

        MPI_Info_get_string(info, "mpi_memory_alloc_kinds",
                            &len, val, &flag);

        while ((kind = strsep(&val, ",")) != NULL) {
            if (strcasecmp(kind, "cuda:managed") == 0) {
                cuda_managed_aware = 1;
            }
            else if (strcasecmp(kind, "cuda:device") == 0) {
                cuda_device_aware = 1;
            }
        }
        free(valptr);
    }

    MPI_Info_free(&info);

    MPI_Group_from_session_pset(session, "mpi://WORLD" , &wgroup);

    // Create a communicator for operations on system memory
    // Usage mode: ASSERTED
    MPI_Info_create(&info);
    MPI_Info_set(info, "mpi_assert_memory_alloc_kinds", "system");
    MPI_Comm_create_from_group(wgroup,
            "org.mpi-forum.side-doc.mem-alloc-kind.cuda-example.system",
            info, MPI_ERRORS_ABORT, &system_comm);
    MPI_Info_free(&info);

    MPI_Comm_size(system_comm, &nranks);

    /*** Check for CUDA awareness ***/

    // Note: MPI does not require homogeneous support
    // across all processes for memory allocation kinds.
    // This example chooses to use
    // CUDA managed allocations (or device allocations)
    // only when all processes report it is supported.

    // Check if all processes have CUDA managed support
    MPI_Allreduce(MPI_IN_PLACE, &cuda_managed_aware, 1, MPI_INT,
                  MPI_LAND, system_comm);

    if (cuda_managed_aware) {
        // Create a communicator for operations that use
        // CUDA managed buffers.
        // Usage mode: ASSERTED
        MPI_Info_create(&info);
        MPI_Info_set(info, "mpi_assert_memory_alloc_kinds",
                     "cuda:managed");
        MPI_Comm_create_from_group(wgroup,
            "org.mpi-forum.side-doc.mem-alloc-kind.cuda-example.managed",
            info, MPI_ERRORS_ABORT, &cuda_managed_comm);
        MPI_Info_free(&info);
    }
    else {
        // Check if all processes have CUDA device support
        MPI_Allreduce(MPI_IN_PLACE, &cuda_device_aware, 1, MPI_INT,
                      MPI_LAND, system_comm);
        if (cuda_device_aware) {
            // Create a communicator for operations that use
            // CUDA device buffers.
            // Usage mode: ASSERTED
            MPI_Info_create(&info);
            MPI_Info_set(info, "mpi_assert_memory_alloc_kinds",
                         "cuda:device");
            MPI_Comm_create_from_group(wgroup,
                "org.mpi-forum.side-doc.mem-alloc-kind.cuda-example.device",
                info, MPI_ERRORS_ABORT, &cuda_device_comm);
            MPI_Info_free(&info);
        }
        else {
            printf("Warning: cuda alloc kind not supported\n");
        }
    }

    MPI_Group_free(&wgroup);

    /*** Execute according to level of CUDA awareness ***/
    if (cuda_managed_aware) {
        // Allocate managed buffer and initialize it
        cudaMallocManaged((void**)&managed_buf, sizeof(int), cudaMemAttachGlobal);
        *managed_buf = 1;

        // Perform communication using cuda_managed_comm
        // if it's available.
        MPI_Allreduce(MPI_IN_PLACE, managed_buf, 1, MPI_INT,
                      MPI_SUM, cuda_managed_comm);

        assert((*managed_buf) == nranks);
        
        cudaFree(managed_buf);
    }
    else {
        // Allocate system buffer and initialize it
        system_buf = (int*)malloc(sizeof(int));
        *system_buf = 1;

        // Allocate CUDA device buffer and initialize it
        cudaMalloc((void**)&device_buf, sizeof(int));
        cudaMemcpyAsync(device_buf, system_buf, sizeof(int),
                   cudaMemcpyHostToDevice, 0);

        cudaStreamSynchronize(0); 
        if (cuda_device_aware) {
            // Perform communication using cuda_device_comm
            // if it's available.
            MPI_Allreduce(MPI_IN_PLACE, device_buf, 1, MPI_INT,
                          MPI_SUM, cuda_device_comm);
            
            assert((*device_buf) == nranks);
        }
        else {
            // Otherwise, copy data to a system buffer,
            // use system_comm, and copy data back to device buffer
            cudaMemcpyAsync(system_buf, device_buf, sizeof(int),
                   cudaMemcpyDeviceToHost, 0);
            
            cudaStreamSynchronize(0); 
            MPI_Allreduce(MPI_IN_PLACE, system_buf, 1, MPI_INT,
                          MPI_SUM, system_comm);
            cudaMemcpyAsync(device_buf, system_buf, sizeof(int),
                   cudaMemcpyHostToDevice, 0);

            cudaStreamSynchronize(0); 
            assert((*system_buf) == nranks);
        }

        cudaFree(device_buf);
        free(system_buf);
    }

    if (cuda_managed_comm != MPI_COMM_NULL)
        MPI_Comm_disconnect(&cuda_managed_comm);
    if (cuda_device_comm != MPI_COMM_NULL)
        MPI_Comm_disconnect(&cuda_device_comm);
    MPI_Comm_disconnect(&system_comm);

    MPI_Session_finalize(&session);

    return 0;
}
