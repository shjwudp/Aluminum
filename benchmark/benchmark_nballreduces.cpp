#include <iostream>
#include "Al.hpp"
#include "test_utils.hpp"
#ifdef AL_HAS_NCCL
#include "test_utils_nccl_cuda.hpp"
#endif
#ifdef AL_HAS_MPI_CUDA
#include "test_utils_mpi_cuda.hpp"
#endif

size_t start_size = 1;
size_t max_size = 1<<30;
const size_t num_trials = 10;

template <typename Backend>
void time_allreduce_algo(typename VectorType<Backend>::type input,
                         typename Backend::comm_type& comm,
                         typename Backend::algo_type algo) {
  std::vector<double> times, in_place_times;
  for (size_t trial = 0; trial < num_trials + 1; ++trial) {
    auto recv = get_vector<Backend>(input.size());
    auto in_place_input(input);    
    typename Backend::req_type req = get_request<Backend>();
    MPI_Barrier(MPI_COMM_WORLD);
    start_timer<Backend>(comm);
    Al::NonblockingAllreduce<Backend>(
        input.data(), recv.data(), input.size(),
        Al::ReductionOperator::sum, comm, req, algo);
    Al::Wait<Backend>(req);
    times.push_back(finish_timer<Backend>(comm));
    MPI_Barrier(MPI_COMM_WORLD);
    start_timer<Backend>(comm);
    Al::NonblockingAllreduce<Backend>(
        in_place_input.data(), input.size(),
        Al::ReductionOperator::sum, comm, req, algo);
    Al::Wait<Backend>(req);
    in_place_times.push_back(finish_timer<Backend>(comm));
  }
  // Delete warmup trial.
  times.erase(times.begin());
  in_place_times.erase(in_place_times.begin());
  if (comm.rank() == 0) {
    std::cout << "size=" << input.size() << " algo=" << static_cast<int>(algo)
              << " regular ";
    print_stats(times);
    std::cout << "size=" << input.size() << " algo=" << static_cast<int>(algo)
              << " inplace ";
    print_stats(in_place_times);
  }
}

template <typename Backend>
void time_allreduce_algo_start(typename VectorType<Backend>::type input,
                               typename Backend::comm_type& comm,
                               typename Backend::algo_type algo) {
  std::vector<double> times, in_place_times;
  for (size_t trial = 0; trial < num_trials + 1; ++trial) {
    auto recv = get_vector<Backend>(input.size());
    auto in_place_input(input);    
    typename Backend::req_type req = get_request<Backend>();
    MPI_Barrier(MPI_COMM_WORLD);
    start_timer<Backend>(comm);
    Al::NonblockingAllreduce<Backend>(
        input.data(), recv.data(), input.size(),
        Al::ReductionOperator::sum, comm, req, algo);
    times.push_back(finish_timer<Backend>(comm));
    Al::Wait<Backend>(req);
    MPI_Barrier(MPI_COMM_WORLD);
    start_timer<Backend>(comm);
    Al::NonblockingAllreduce<Backend>(
        in_place_input.data(), input.size(),
        Al::ReductionOperator::sum, comm, req, algo);
    in_place_times.push_back(finish_timer<Backend>(comm));
    Al::Wait<Backend>(req);
  }
  // Delete warmup trial.
  times.erase(times.begin());
  in_place_times.erase(in_place_times.begin());
  if (comm.rank() == 0) {
    std::cout << "size=" << input.size() << " algo=" << static_cast<int>(algo)
              << " regular start ";
    print_stats(times);
    std::cout << "size=" << input.size() << " algo=" << static_cast<int>(algo)
              << " inplace start ";
    print_stats(in_place_times);
  }
}

template <typename Backend>
void time_allreduce_algo_wait(typename VectorType<Backend>::type input,
                              typename Backend::comm_type& comm,
                              typename Backend::algo_type algo) {
  std::vector<double> times, in_place_times;
  for (size_t trial = 0; trial < num_trials + 1; ++trial) {
    auto recv = get_vector<Backend>(input.size());
    auto in_place_input(input);    
    typename Backend::req_type req = get_request<Backend>();
    MPI_Barrier(MPI_COMM_WORLD);
    Al::NonblockingAllreduce<Backend>(
        input.data(), recv.data(), input.size(),
        Al::ReductionOperator::sum, comm, req, algo);
    start_timer<Backend>(comm);
    Al::Wait<Backend>(req);
    times.push_back(finish_timer<Backend>(comm));
    MPI_Barrier(MPI_COMM_WORLD);
    Al::NonblockingAllreduce<Backend>(
        in_place_input.data(), input.size(),
        Al::ReductionOperator::sum, comm, req, algo);
    start_timer<Backend>(comm);
    Al::Wait<Backend>(req);
    in_place_times.push_back(finish_timer<Backend>(comm));
  }
  // Delete warmup trial.
  times.erase(times.begin());
  in_place_times.erase(in_place_times.begin());
  if (comm.rank() == 0) {
    std::cout << "size=" << input.size() << " algo=" << static_cast<int>(algo)
              << " regular wait ";
    print_stats(times);
    std::cout << "size=" << input.size() << " algo=" << static_cast<int>(algo)
              << " inplace wait ";
    print_stats(in_place_times);
  }
}

template <typename Backend>
void do_benchmark() {
  std::vector<typename Backend::algo_type> algos
      = get_nb_allreduce_algorithms<Backend>();
  typename Backend::comm_type comm;  // Use COMM_WORLD.
  std::vector<size_t> sizes = {0};
  for (size_t size = start_size; size <= max_size; size *= 2) {
    sizes.push_back(size);
  }
  for (const auto& size : sizes) {
    auto data = gen_data<Backend>(size);
    // Benchmark algorithms.
    for (auto&& algo : algos) {
      time_allreduce_algo<Backend>(data, comm, algo);
      time_allreduce_algo_start<Backend>(data, comm, algo);
      time_allreduce_algo_wait<Backend>(data, comm, algo);
    }
  }
}


int main(int argc, char** argv) {
#ifdef AL_HAS_CUDA
  set_device();
#endif
  Al::Initialize(argc, argv);

  std::string backend = "MPI";
  if (argc >= 2) {
    backend = argv[1];
  }
  if (argc == 3) {
    max_size = std::atoi(argv[2]);
  }
  if (argc == 4) {
    start_size = std::atoi(argv[2]);
    max_size = std::atoi(argv[3]);
  }

  if (backend == "MPI") {
    do_benchmark<Al::MPIBackend>();
#ifdef AL_HAS_NCCL
  } else if (backend == "NCCL") {
    do_benchmark<Al::NCCLBackend>();
#endif    
#ifdef AL_HAS_MPI_CUDA
  } else if (backend == "MPI-CUDA") {
    do_benchmark<Al::MPICUDABackend>();
#endif    
  } else {
    std::cerr << "usage: " << argv[0] << " [MPI";
#ifdef AL_HAS_NCCL
    std::cerr << " | NCCL";
#endif
#ifdef AL_HAS_MPI_CUDA
    std::cerr << " | MPI-CUDA";
#endif
    std::cerr << "]" << std::endl;
    return -1;
  }

  Al::Finalize();
  return 0;
}
