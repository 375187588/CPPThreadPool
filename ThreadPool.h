#pragma once
#include <unordered_map>
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <condition_variable>

namespace TPool {
	//linux :g++ -v
	//linux :g++ -fPIC -shared ThreadPool.cpp -o libtpool.so -std=c++17
	//linux :error-> 'std::thread' has not been declared
	// void start(int initThreadSize=std::thread::hardware_concurrency());
	//linux下一般是从:/usr/lib /usr/local/lib 下去找 .a和.so库文件
	//从:/usr/include /usr/local/include 下去找 *.h头文件
	// su root
	// Password:输入root密码
	// 在生成的libtpool.so复制到如下目录，执行如下命令:
	// mv libtpool.so /usr/local/lib
	// mv ThreadPool.h /usr/local/include
	// 查看命令：ls /usr/local/lib/libtpool.so
	//之后就可以删除ThreadPool.cpp文件 执行如下命令:
	// rm ThreadPool.cpp
	// g++ main.cpp -std=c++17 -ltpool -lpthread
	// ./a.out
	// 会报错：error while loading shared libraries: libtpool.so:
	// cannot open shared object file:No such file or director
	// 
	// 解决上面问题方法：
	// vim /etc/ld.so.c
	// ld.so.cache ld.so.conf ld.so.conf.d/
	// 找so库会从ld.so.cache中去找
	// vim /etc/ld.so.conf
	// cd /etc/ld.so.conf.d/
	// vim mylib.conf
	//加入如下路径:/usr/local/lib
	// 在wq保存
	// 在cd 到main.cpp所在目录去执行如下命令：
	// ldconfig
	// ./a.out
	// 
	// ps -u
	// root 6359 0.7 0.0 385764 1508 pts/1 s1_ 00:56 0:01 ./a.out
	// 通过ps -u查到./a.out进程运行的PID
	// gdb attach 6359
	// (gdb) info threads
	// bt
	// thread 5//是执行bt后查到的线程ID号 最前面的数字
	// bt
	// 在vs下 mutex class condition_variable{
	// public:
	// using native_handle_type = _Cnd_t;
	// condition_variable(){
	// _Cnd_init_in_situ(_mYCND()));
	// }
	// ~condition_variable() noexcept{
	// _Cnd_destroy_in_situ(_Mycnd());
	// }
	// 在VS下条件变量的类有构造
	// find / -name condition_variable
	// /opt/rh/devtoolset-7/root/usr/include/c++/7/condition_variable
	// /usr/include/c++/4.8.2/condition_variable
	// vim /opt/rh/devtoolset-7/root/usr/include/c++/7/condition_variable
	// class condition_variable
	// {
	// public:
	// typedef __native_type* native_handle_t;
	// condition_variable() noexcept;
	// ~condition_variable() noexcept;
	// 这儿析构
	// 
	//Any类型：可以接收任意的数据类型
	class Any
	{
	public:
		Any() = default;
		~Any() = default;
		Any(const Any&) = delete;
		Any& operator=(const Any&) = delete;
		Any(Any&&) = default;
		Any& operator=(Any&&) = default;

		//这个构造函数可以让Any类型接收任意其它的数据
		template<typename T>
		Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

		//这个方法能把Any对象里面存储的data数据提取出来
		template<typename T>
		T cast_()
		{
			//我们怎么从base_找到它所指向的Derive对象，从它里面取出data成员变量
			//基类指针->派生类指针 RTTI
			Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
			if (pd == nullptr)
			{
				throw "type is unmatch!";
			}
			return pd->data_;
		}

	private:
		class Base
		{
		public:
			virtual ~Base() = default;
		};

		template<typename T>
		class Derive :public Base
		{
		public:
			Derive(T data) :data_(data)
			{

			}
		public:
			T data_;//保存了任意的其它类型
		};

	private:
		//定义一个基类的指针
		std::unique_ptr<Base> base_;
	};

	//实现一个信号量类
	class Semaphore
	{
	public:
		Semaphore(int limit = 0) :
			resLimit_(limit)
			, isExit_(false)
		{

		}
		~Semaphore()
		{
			isExit_ = true;
		}
		//获取一个信号量资源
		void wait()
		{
			if (isExit_)
				return;
			std::unique_lock<std::mutex> lock(mtx_);
			//等待信号量有资源，没有资源的话会阻塞当前线程
			cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
			resLimit_--;
		}

		//增加一个信号量资源
		void post()
		{
			if (isExit_)
				return;

			std::unique_lock<std::mutex> lock(mtx_);
			resLimit_++;
			//等待状态，释放mutex锁，通知条件变量wait的地方，可以起来干活了
			
			//linux下condition_variable什么也没有做
			cond_.notify_all();
		}

	private:
		std::atomic_bool isExit_;
		int resLimit_;
		std::mutex mtx_;
		std::condition_variable cond_;
	};

	class Thread
	{
	public:
		using ThreadFunc = std::function<void(int)>;

		Thread(ThreadFunc func);
		~Thread();

		void start();
		int getId() const;

	private:
		ThreadFunc func_;

		static int generateId_;
		int threadId_;//保存线程ID
	};

	class Result;
	//任务抽象基类
	class Task
	{
	public:
		Task();
		~Task() = default;
		void exec();
		//用户可以自定义任意任务类型，从Task继承,重写run方法,实现自定义任务处理
		virtual Any run() = 0;

		void setResult(Result* res);

	private:
		Result* result_;//Result对象的声明
	};

	enum class PoolMode
	{
		MODE_FIXED,//固定数量的线程模式 比较耗时
		MODE_CACHED//线程数量可动态增长模式 任务处理比较紧急，场景：小而快的任务
	};

	class Result
	{
	public:
		Result(std::shared_ptr<Task> task, bool isValid = true);
		~Result() = default;

		//setVal方法 获取任务执行完成后的返回值
		void setVal(Any any);

		//get方法，用户调用这个方法获取task的返回值
		Any get();


	private:
		std::atomic_bool isValid_;//返回值是否有效
		//指向获取对应返回值的对象
		std::shared_ptr<Task> task_;
		Any any_;//存储任务的返回值
		Semaphore sem_;//	
	};

	class ThreadPool
	{
	public:
		ThreadPool();
		~ThreadPool();
		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;

	public:
		void setMode(PoolMode mode);
		void setTaskQueMaxThreashHold(int threshhold);
		void setThreadSizeThreshHold(int threshhold);
		bool checkRunningState() const;
		void start(int initThreadSize=std::thread::hardware_concurrency());
		Result submitTask(std::shared_ptr<Task> sp);

	private:
		void threadFunc(int threadid);

	private:
		std::unordered_map<int, std::unique_ptr<Thread>> threads_;//线程列表

		int initThreadSize_;//初始的线程数量
		int threadSizeThreshHold_;//线程数量上限阈值
		std::atomic_int curThreadSize_;//记录当前线程池里面线程的总数量
		std::atomic_int idleThreadSize_;//记录空闲线程的数量

		std::queue<std::shared_ptr<Task>> taskQue_;//任务队列
		std::atomic_int taskSize_;//任务的数量
		int taskQueMaxThreshHold_;;//任务队列数量上限阈值

		std::mutex taskQueMtx_;//保证任务队列的线程安全
		std::condition_variable notFull_;//表示任务队列不满
		std::condition_variable notEmpty_;//表示任务队列不空
		std::condition_variable exitCond_;//等待线程资源全部回收

		PoolMode poolMode_;//当前线程池的工作模式
		std::atomic_bool isPoolRunning_;//表示当前线程池的启动状态
	};
};
