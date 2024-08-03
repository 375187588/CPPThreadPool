// ThreadPoolApp.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "ThreadPool.h"
namespace TPool {
    class MyTask : public TPool::Task
    {
    public:
        MyTask(unsigned long long begin, unsigned long long end)
            : begin_(begin)
            , end_(end) {}

        TPool::Any run()
        {
            std::cout << std::this_thread::get_id() << " begin!" << std::endl;
            unsigned long long sum = 0;
            for (unsigned long long i = begin_; i < end_; i++)
            {
                sum += i;
            }
            //std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << std::this_thread::get_id() << " end!" << std::endl;

            return sum;
        }

    private:
        unsigned long long begin_;
        unsigned long long end_;
    };

    int main()
    {
        std::cout << "Hello World!\n";
        unsigned long long result = 0;
        {
            TPool::ThreadPool pool;
            pool.setMode(TPool::PoolMode::MODE_CACHED);
            //pool.setMode(TPool::PoolMode::MODE_FIXED);

            pool.start(4);

            TPool::Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 2000000));
            TPool::Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 2000000));
            TPool::Result res3 = pool.submitTask(std::make_shared<MyTask>(1, 2000000));
            TPool::Result res4 = pool.submitTask(std::make_shared<MyTask>(1, 2000000));
            TPool::Result res5 = pool.submitTask(std::make_shared<MyTask>(1, 2000000));
            TPool::Result res6 = pool.submitTask(std::make_shared<MyTask>(1, 2000000));

            unsigned long long sum1 = res1.get().cast_<unsigned long long>();
            unsigned long long sum2 = res2.get().cast_<unsigned long long>();
            unsigned long long sum3 = res3.get().cast_<unsigned long long>();
            unsigned long long sum4 = res4.get().cast_<unsigned long long>();
            unsigned long long sum5 = res5.get().cast_<unsigned long long>();
            unsigned long long sum6 = res6.get().cast_<unsigned long long>();

            result = (sum1 + sum2 + sum3 + sum4 + sum5 + sum6);
        }

        //Master- Slave线程模型
        //Master线程用来分解任务，然后给各个Slave线程分配任务
        //等待各个Slave线程执行完成任务，返回结果
        //Master线程合并各个任务结果，输出
        std::cout << "thread calc result:" << result << std::endl;
        std::cout << "main over" << std::endl;


        //std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }
};
/*
如何能让线程
1：pool.submitTask(sum1,10,20);
pool.submitTask(sum2,1,2,3);
submitTask:可变参模板编程
2：我们自己造了一个Result以及相关的类型，代码挺多
c++11线程库 thread packaged_task(function函数对象) async
使用future来代替Result
*/

#include <future>
#include <thread>
#include "ThreadPoolEx.h"

namespace TPoolEx
{
    int sum1(int a, int b)
    {
        std::this_thread::sleep_for(std::chrono::seconds(21));
        return a + b;
    }
    int sum2(int a, int b, int c)
    {
        std::this_thread::sleep_for(std::chrono::seconds(7));
        return a + b + c;
    }

    int main2()
    {
        std::packaged_task<int(int, int)> task(sum1);
        //future<->Result
        std::future<int> res = task.get_future();
        task(10, 20);

        std::thread t1(std::move(task), 10, 20);
        t1.detach();
        std::cout << res.get() << std::endl;

        //std::cout << "Hello World!\n";
        //std::thread t1(sum1, 10, 20);
        //std::thread t2(sum2, 1, 2, 3);
        //t1.join();
        //t2.join();


        std::cout << "main over" << std::endl;


        //std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }

    int main()
    {
        TPoolEx::ThreadPool pool;
        pool.setTaskQueMaxThreashHold(2);
       // pool.setMode(TPoolEx::PoolMode::MODE_CACHED);
        pool.start(2);
       
        std::future<int>  r1 = pool.submitTask(sum1, 1, 2);
        std::future<int>  r2 = pool.submitTask(sum2, 1, 2,3);
        std::future<int>  r3 = pool.submitTask([](int begin, int end)->int {
            int sum = 0;
            for (int i = begin; i <= end; i++)
            {
                sum += i;
            }
            return sum;
            }, 1, 100);
        std::future<int>  r4 = pool.submitTask([](int begin, int end)->int {
            int sum = 0;
            for (int i = begin; i <= end; i++)
            {
                sum += i;
            }
            return sum;
            }, 1, 100);
        std::future<int>  r5 = pool.submitTask([](int begin, int end)->int {
            int sum = 0;
            for (int i = begin; i <= end; i++)
            {
                sum += i;
            }
            return sum;
            }, 1, 100);

        std::cout << r1.get() << std::endl;
        std::cout << r2.get() << std::endl;
        std::cout << r3.get() << std::endl;
        std::cout << r4.get() << std::endl;
        std::cout << r5.get() << std::endl;
        std::cout << "main over" << std::endl;
       // getchar();
        return 0;
    }
};

int main()
{
    return TPoolEx::main();
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 

//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
