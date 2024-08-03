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
	//linux��һ���Ǵ�:/usr/lib /usr/local/lib ��ȥ�� .a��.so���ļ�
	//��:/usr/include /usr/local/include ��ȥ�� *.hͷ�ļ�
	// su root
	// Password:����root����
	// �����ɵ�libtpool.so���Ƶ�����Ŀ¼��ִ����������:
	// mv libtpool.so /usr/local/lib
	// mv ThreadPool.h /usr/local/include
	// �鿴���ls /usr/local/lib/libtpool.so
	//֮��Ϳ���ɾ��ThreadPool.cpp�ļ� ִ����������:
	// rm ThreadPool.cpp
	// g++ main.cpp -std=c++17 -ltpool -lpthread
	// ./a.out
	// �ᱨ��error while loading shared libraries: libtpool.so:
	// cannot open shared object file:No such file or director
	// 
	// ����������ⷽ����
	// vim /etc/ld.so.c
	// ld.so.cache ld.so.conf ld.so.conf.d/
	// ��so����ld.so.cache��ȥ��
	// vim /etc/ld.so.conf
	// cd /etc/ld.so.conf.d/
	// vim mylib.conf
	//��������·��:/usr/local/lib
	// ��wq����
	// ��cd ��main.cpp����Ŀ¼ȥִ���������
	// ldconfig
	// ./a.out
	// 
	// ps -u
	// root 6359 0.7 0.0 385764 1508 pts/1 s1_ 00:56 0:01 ./a.out
	// ͨ��ps -u�鵽./a.out�������е�PID
	// gdb attach 6359
	// (gdb) info threads
	// bt
	// thread 5//��ִ��bt��鵽���߳�ID�� ��ǰ�������
	// bt
	// ��vs�� mutex class condition_variable{
	// public:
	// using native_handle_type = _Cnd_t;
	// condition_variable(){
	// _Cnd_init_in_situ(_mYCND()));
	// }
	// ~condition_variable() noexcept{
	// _Cnd_destroy_in_situ(_Mycnd());
	// }
	// ��VS���������������й���
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
	// �������
	// 
	//Any���ͣ����Խ����������������
	class Any
	{
	public:
		Any() = default;
		~Any() = default;
		Any(const Any&) = delete;
		Any& operator=(const Any&) = delete;
		Any(Any&&) = default;
		Any& operator=(Any&&) = default;

		//������캯��������Any���ͽ�����������������
		template<typename T>
		Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

		//��������ܰ�Any��������洢��data������ȡ����
		template<typename T>
		T cast_()
		{
			//������ô��base_�ҵ�����ָ���Derive���󣬴�������ȡ��data��Ա����
			//����ָ��->������ָ�� RTTI
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
			T data_;//�������������������
		};

	private:
		//����һ�������ָ��
		std::unique_ptr<Base> base_;
	};

	//ʵ��һ���ź�����
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
		//��ȡһ���ź�����Դ
		void wait()
		{
			if (isExit_)
				return;
			std::unique_lock<std::mutex> lock(mtx_);
			//�ȴ��ź�������Դ��û����Դ�Ļ���������ǰ�߳�
			cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
			resLimit_--;
		}

		//����һ���ź�����Դ
		void post()
		{
			if (isExit_)
				return;

			std::unique_lock<std::mutex> lock(mtx_);
			resLimit_++;
			//�ȴ�״̬���ͷ�mutex����֪ͨ��������wait�ĵط������������ɻ���
			
			//linux��condition_variableʲôҲû����
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
		int threadId_;//�����߳�ID
	};

	class Result;
	//����������
	class Task
	{
	public:
		Task();
		~Task() = default;
		void exec();
		//�û������Զ��������������ͣ���Task�̳�,��дrun����,ʵ���Զ���������
		virtual Any run() = 0;

		void setResult(Result* res);

	private:
		Result* result_;//Result���������
	};

	enum class PoolMode
	{
		MODE_FIXED,//�̶��������߳�ģʽ �ȽϺ�ʱ
		MODE_CACHED//�߳������ɶ�̬����ģʽ ������ȽϽ�����������С���������
	};

	class Result
	{
	public:
		Result(std::shared_ptr<Task> task, bool isValid = true);
		~Result() = default;

		//setVal���� ��ȡ����ִ����ɺ�ķ���ֵ
		void setVal(Any any);

		//get�������û��������������ȡtask�ķ���ֵ
		Any get();


	private:
		std::atomic_bool isValid_;//����ֵ�Ƿ���Ч
		//ָ���ȡ��Ӧ����ֵ�Ķ���
		std::shared_ptr<Task> task_;
		Any any_;//�洢����ķ���ֵ
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
		std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�

		int initThreadSize_;//��ʼ���߳�����
		int threadSizeThreshHold_;//�߳�����������ֵ
		std::atomic_int curThreadSize_;//��¼��ǰ�̳߳������̵߳�������
		std::atomic_int idleThreadSize_;//��¼�����̵߳�����

		std::queue<std::shared_ptr<Task>> taskQue_;//�������
		std::atomic_int taskSize_;//���������
		int taskQueMaxThreshHold_;;//�����������������ֵ

		std::mutex taskQueMtx_;//��֤������е��̰߳�ȫ
		std::condition_variable notFull_;//��ʾ������в���
		std::condition_variable notEmpty_;//��ʾ������в���
		std::condition_variable exitCond_;//�ȴ��߳���Դȫ������

		PoolMode poolMode_;//��ǰ�̳߳صĹ���ģʽ
		std::atomic_bool isPoolRunning_;//��ʾ��ǰ�̳߳ص�����״̬
	};
};
