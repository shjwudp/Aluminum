////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2018, Lawrence Livermore National Security, LLC.  Produced at the
// Lawrence Livermore National Laboratory in collaboration with University of
// Illinois Urbana-Champaign.
//
// Written by the LBANN Research Team (N. Dryden, N. Maruyama, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-756777.
// All rights reserved.
//
// This file is part of Aluminum GPU-aware Communication Library. For details, see
// http://software.llnl.gov/Aluminum or https://github.com/LLNL/Aluminum.
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include "Al.hpp"
#include "test_utils.hpp"
#ifdef AL_HAS_NCCL
#include "test_utils_nccl_cuda.hpp"
#endif
#ifdef AL_HAS_HOST_TRANSFER
#include "test_utils_ht.hpp"
#endif

#include <stdlib.h>
#include <math.h>
#include <string>

// Size is the per-rank send size.
size_t start_size = 1;
size_t max_size = 1<<30;

// For simplicity, the allgatherv is equivalent to an allgather here.

/**
 * Test allgatherv algo on input, check with expected.
 */
template <typename Backend>
void test_allgatherv_algo(const typename VectorType<Backend>::type& expected,
                          const typename VectorType<Backend>::type& expected_inplace,
                          typename VectorType<Backend>::type input,
                          typename VectorType<Backend>::type input_inplace,
                          typename Backend::comm_type& comm,
                          typename Backend::allgather_algo_type algo) {
  auto recv = get_vector<Backend>(input.size() * comm.size());
  std::vector<size_t> counts = std::vector<size_t>(comm.size(), input.size());
  std::vector<size_t> displs = Al::excl_prefix_sum(counts);
  // Test regular allgatherv.
  Al::Allgatherv<Backend>(input.data(), recv.data(), counts, displs,
                          comm, algo);
  if (!check_vector(expected, recv)) {
    std::cout << comm.rank() << ": regular allgatherv does not match" <<
        std::endl;
    std::abort();
  }
  MPI_Barrier(MPI_COMM_WORLD);
  // Test in-place allgather.
  Al::Allgatherv<Backend>(input_inplace.data(), counts, displs,
                         comm, algo);
  MPI_Barrier(MPI_COMM_WORLD);
  if (!check_vector(expected_inplace, input_inplace)) {
    std::cout << comm.rank() << ": in-place allgatherv does not match" <<
      std::endl;
    std::abort();
  }
}

/**
 * Test non-blocking allgatherv algo on input, check with expected.
 */
template <typename Backend>
void test_nb_allgatherv_algo(const typename VectorType<Backend>::type& expected,
                             const typename VectorType<Backend>::type& expected_inplace,
                             typename VectorType<Backend>::type input,
                             typename VectorType<Backend>::type input_inplace,
                             typename Backend::comm_type& comm,
                             typename Backend::allgather_algo_type algo) {
  typename Backend::req_type req = get_request<Backend>();
  auto recv = get_vector<Backend>(input.size() * comm.size());
  std::vector<size_t> counts = std::vector<size_t>(comm.size(), input.size());
  std::vector<size_t> displs = Al::excl_prefix_sum(counts);
  // Test regular allgatherv.
  Al::NonblockingAllgatherv<Backend>(input.data(), recv.data(),
                                     counts, displs, comm, req, algo);
  Al::Wait<Backend>(req);
  if (!check_vector(expected, recv)) {
    std::cout << comm.rank() << ": regular allgatherv does not match" <<
      std::endl;
    std::abort();
  }
  MPI_Barrier(MPI_COMM_WORLD);
  // Test in-place allgatherv.
  Al::NonblockingAllgatherv<Backend>(input_inplace.data(),
                                     counts, displs,
                                     comm, req, algo);
  Al::Wait<Backend>(req);
  if (!check_vector(expected_inplace, input_inplace)) {
    std::cout << comm.rank() << ": in-place allgatherv does not match" <<
      std::endl;
    std::abort();
  }
}

template <typename Backend>
void test_correctness() {
  auto algos = get_allgatherv_algorithms<Backend>();
  auto nb_algos = get_nb_allgatherv_algorithms<Backend>();
  typename Backend::comm_type comm = get_comm_with_stream<Backend>(MPI_COMM_WORLD);
  // Compute sizes to test.
  std::vector<size_t> sizes = get_sizes(start_size, max_size, true);
  for (const auto& size : sizes) {
    if (comm.rank() == 0) {
      std::cout << "Testing size " << human_readable_size(size) << std::endl;
    }
    // Compute true value.
    size_t global_size = size * comm.size();
    typename VectorType<Backend>::type &&data = gen_data<Backend>(size);
    auto expected = get_vector<Backend>(global_size);
    get_expected_allgather_result(data, expected);
    typename VectorType<Backend>::type &&data_inplace = gen_data<Backend>(global_size);
    auto expected_inplace(data_inplace);
    get_expected_allgather_inplace_result(expected_inplace);
    // Test algorithms.
    for (auto&& algo : algos) {
      MPI_Barrier(MPI_COMM_WORLD);
      if (comm.rank() == 0) {
        std::cout << " Algo: " << Al::algorithm_name(algo) << std::endl;
      }
      test_allgatherv_algo<Backend>(expected, expected_inplace,
                                    data, data_inplace, comm, algo);
    }
    for (auto&& algo : nb_algos) {
      MPI_Barrier(MPI_COMM_WORLD);
      if (comm.rank() == 0) {
        std::cout << " Algo: NB " << Al::algorithm_name(algo) << std::endl;
      }
      test_nb_allgatherv_algo<Backend>(expected, expected_inplace,
                                       data, data_inplace, comm, algo);
    }
  }
  free_comm_with_stream<Backend>(comm);
}

int main(int argc, char** argv) {
  // Need to set the CUDA device before initializing Aluminum.
#ifdef AL_HAS_CUDA
  set_device();
#endif
  Al::Initialize(argc, argv);

  std::string backend = "MPI";
  parse_args(argc, argv, backend, start_size, max_size);

  if (backend == "MPI") {
    test_correctness<Al::MPIBackend>();
#ifdef AL_HAS_NCCL
  } else if (backend == "NCCL") {
    test_correctness<Al::NCCLBackend>();
#endif
#ifdef AL_HAS_MPI_CUDA
  } else if (backend == "MPI-CUDA") {
    std::cerr << "Allgatherv not supported on MPI-CUDA backend." << std::endl;
    std::abort();
#endif
#ifdef AL_HAS_HOST_TRANSFER
  } else if (backend == "HT") {
    //test_correctness<Al::HostTransferBackend>();
#endif
  }

  Al::Finalize();
  return 0;
}
