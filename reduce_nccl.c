#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mpi.h>
#include <cuda_runtime.h>
#include <nccl.h>

int main(int argc, char **argv)
{
  int i;
  int myid, nprocs, ierr, provided;
  MPI_Status status;
  int N = 1000, loops;
  double time;
  double *rbuf, *sbuf;
  double *d_rbuf, *d_sbuf;
  ncclResult_t ncclRet;
  ncclComm_t comm;
  cudaStream_t stream;
  ncclUniqueId commId;

  if(argc!=3){
	printf("usage: %s length loops\n", argv[0]);
	return -1;
  }

  N = atoi(argv[1]);
  loops = atoi(argv[2]);

  MPI_Init(&argc, &argv);
  /*
  ierr = MPI_Init_thread(&argc,&argv,MPI_THREAD_FUNNELED,&provided);
  if(provided!=MPI_THREAD_FUNNELED)printf("MPI_THREAD_FUNNELED is not provided.\n");
  */
  ierr = MPI_Comm_rank(MPI_COMM_WORLD, &myid);
  ierr = MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  if(nprocs!=2){
	printf("2 processes are required.\n");
	return -1;
  }
  rbuf = (double*)malloc(sizeof(double)*N);
  sbuf = (double*)malloc(sizeof(double)*N);
  for(i=0; i<N; i++)rbuf[i] = 0.0;
  for(i=0; i<N; i++)sbuf[i] = (double)(myid+1);
  cudaMalloc((void*)&d_rbuf, sizeof(double)*N);
  cudaMalloc((void*)&d_sbuf, sizeof(double)*N);
  cudaMemcpy(&d_rbuf, rbuf, sizeof(double)*N, cudaMemcpyHostToDevice);
  cudaMemcpy(&d_sbuf, sbuf, sizeof(double)*N, cudaMemcpyHostToDevice);

  if(myid==0){
    ncclRet = ncclGetUniqueId(&commId);
    if(ncclRet!=ncclSuccess){
      printf("ncclGetUniqueId failed: %d\n", ncclRet);
      printf("%s", ncclGetErrorString(ncclRet));
      return -1;
    }
    printf("%d: commId = %d\n", myid, commId);
  }
  MPI_Barrier(MPI_COMM_WORLD);

  MPI_Bcast(&commId, NCCL_UNIQUE_ID_BYTES, MPI_CHAR, 0, MPI_COMM_WORLD);
  cudaSetDevice(0);
  ncclRet = ncclCommInitRank(&comm, nprocs, commId, myid);
  if(ncclRet!=ncclSuccess){
    printf("ncclCommInitRank failed: %d\n", ncclRet);
    printf("%s", ncclGetErrorString(ncclRet));
    return -1;
  }

  cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
  printf("before ncclReduce\n"); fflush(stdout);
  ierr = MPI_Barrier(MPI_COMM_WORLD);
  time = MPI_Wtime();
  ncclGroupStart();
  //  for(i=0; i<loops; i++){
    //ierr = MPI_Reduce(d_sbuf, d_rbuf, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  //ncclRet = ncclReduce((const void*)d_sbuf, (void*)d_rbuf, 1, ncclDouble, ncclSum, 0, comm, stream);
  ncclRet = ncclAllReduce((const void*)d_sbuf, (void*)d_rbuf, 1, ncclDouble, ncclSum, comm, stream);
  if(ncclRet!=ncclSuccess){
    printf("ncclReduce failed: %d\n", ncclRet);
    printf("%s", ncclGetErrorString(ncclRet));
    return -1;
  }
  cudaStreamSynchronize(stream);
  ncclGroupEnd();
    //}
  time = MPI_Wtime() - time;
  printf("after  ncclReduce\n"); fflush(stdout);
  cudaStreamDestroy(stream);

  cudaMemcpy(rbuf, d_rbuf, sizeof(double)*N, cudaMemcpyDeviceToHost);
  if(myid==0){
    printf("result: %e\n", rbuf[0]);
  }
  ierr = MPI_Barrier(MPI_COMM_WORLD);
  printf("TIME %d : %e (average %e msec)\n", myid, time, time/(double)loops*1000.0);

  cudaFree(d_rbuf);
  cudaFree(d_sbuf);
  free(rbuf);
  free(sbuf);

  ierr = MPI_Finalize();
  ncclCommDestroy(comm);

  return 0;
}
