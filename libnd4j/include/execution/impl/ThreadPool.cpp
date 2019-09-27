/*******************************************************************************
 * Copyright (c) 2015-2018 Skymind, Inc.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License, Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

//
// @author raver119@gmail.com
//

#include <execution/ThreadPool.h>
#include <stdexcept>
#include <helpers/logger.h>

namespace samediff {

    // this function executed once per thread, it polls functions from queue, and executes them via wrapper
    static void _executionLoop(int thread_id, BlockingQueue<CallableWithArguments*> *queue) {
        while (true) {
            // this method blocks until there's something within queue
            auto c = queue->poll();
            switch (c->dimensions()) {
                case 1: {
                        auto args = c->arguments();
                        c->function_1d()(args[0], args[1], args[2]);
                        c->finish();
                    }
                    break;
                case 2: {
                        auto args = c->arguments();
                        c->function_2d()(args[0], args[1], args[2], args[3], args[4], args[5]);
                        c->finish();
                    }
                    break;
                case 3:
                default:
                    throw std::runtime_error("Don't know what to do with provided Callable");
            }
        }
    }

    ThreadPool::ThreadPool() {
        // TODO: number of threads must reflect number of cores for UMA system. In case of NUMA it should be per-device pool
        // FIXME: on mobile phones this feature must NOT be used
        _available = std::thread::hardware_concurrency();

        _queues.resize(_available.load());
        _threads.resize(_available.load());

        // creating threads here
        for (int e = 0; e < _available.load(); e++) {
            _queues[e] = new BlockingQueue<CallableWithArguments*>(2);
            _threads[e] = new std::thread(_executionLoop, e, _queues[e]);

            // now we must set affinity, and it's going to be platform-specific thing
#ifdef LINUX_BUILD
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(e, &cpuset);
            int rc = pthread_setaffinity_np(_threads[e]->native_handle(), sizeof(cpu_set_t), &cpuset);
            if (rc != 0)
                throw std::runtime_error("Failed to set pthread affinity");
#endif
        }
    }

    ThreadPool::~ThreadPool() {
        // TODO: implement this one properly
        for (int e = 0; e < _queues.size(); e++) {
            // stop each and every thread

            // release queue and thread
            //delete _queues[e];
            //delete _threads[e];
        }
    }

    ThreadPool* ThreadPool::getInstance() {
        if (!_INSTANCE)
            _INSTANCE = new ThreadPool();

        return _INSTANCE;
    }

    void ThreadPool::release(int num_threads) {
        _available += num_threads;
    }

    Ticket ThreadPool::tryAcquire(int num_threads) {
        // we check for threads availability here
        _lock.lock();

        bool threaded = false;
        if (_available >= num_threads) {
            threaded = true;
            _available -= num_threads;
        }

        _lock.unlock();

        // we either dispatch tasks to threads, or run single-threaded
        if (threaded) {
            std::vector<BlockingQueue<CallableWithArguments*>*> queues(num_threads);
            for (int e = 0; e < num_threads; e++)
                queues[e] = _queues[e];

            return Ticket(queues);
        } else {
            // if there's no threads available - return empty ticket
            return Ticket();
        }
    }


    ThreadPool* ThreadPool::_INSTANCE = 0;
}
