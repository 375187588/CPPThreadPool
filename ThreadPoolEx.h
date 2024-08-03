#pragma once
#include <unordered_map>
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <future>
#include <condition_variable>
#include <iostream>
namespace TPoolEx {	

	class Thread
	{
	public:
		using ThreadFunc = std::function<void(int)>;

		Thread(ThreadFunc func) :func_(func)
			, threadId_(generateIdEx_++)
		{

		}
		~Thread() = default;

		void start()
		{
			std::thread t(func_, threadId_);
			t.detach();
		}
		int getId() const
		{
			return threadId_;
		}

	private:
		ThreadFunc func_;

		static int generateIdEx_;
		int threadId_;//�����߳�ID
	};

	enum class PoolMode
	{
		MODE_FIXED,//�̶��������߳�ģʽ �ȽϺ�ʱ
		MODE_CACHED//�߳������ɶ�̬����ģʽ ������ȽϽ�����������С���������
	};

	const int TASK_MAX_THRESHHOLD = INT32_MAX;
	const int THREAD_MAX_THRESHHOLD = 1024;
	const int THREAD_MAX_IDLE_TIME = 10;// 60;//��λ����
	class ThreadPool
	{
	public:
		ThreadPool():
			initThreadSize_(0)
			, taskSize_(0)
			, idleThreadSize_(0)
			, curThreadSize_(0)
			, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
			, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
			, poolMode_(PoolMode::MODE_FIXED)
			, isPoolRunning_(false)

		{
		}

		~ThreadPool()
		{
			isPoolRunning_ = false;
			//�ȴ��̳߳��������е��̷߳��� ������״̬������������ִ��������
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			notEmpty_.notify_all();
			exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
		}
		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;

	public:
		void setMode(PoolMode mode)
		{
			if (checkRunningState())
				return;
			poolMode_ = mode;
		}
		void setTaskQueMaxThreashHold(int threshhold)
		{
			if (checkRunningState())
				return;

			taskQueMaxThreshHold_ = threshhold;
		}
		void setThreadSizeThreshHold(int threshhold)
		{
			if (checkRunningState())
				return;

			if (poolMode_ == PoolMode::MODE_CACHED)
			{
				threadSizeThreshHold_ = threshhold;
			}
		}
		bool checkRunningState() const
		{
			return isPoolRunning_;
		}
		void start(int initThreadSize = std::thread::hardware_concurrency())
		{
			std::cout << "initThreadSize:" << initThreadSize << std::endl;
			//�����̳߳صĶ���״̬
			isPoolRunning_ = true;
			//��¼��ʼ�̸߳���
			initThreadSize_ = initThreadSize;
			curThreadSize_ = initThreadSize;

			//�����̶߳���
			for (int i = 0; i < initThreadSize; ++i)
			{
				//����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
				auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
				int threadId = ptr->getId();
				threads_.emplace(threadId, std::move(ptr));
			}
			//���������߳�
			for (int i = 0; i < initThreadSize_; ++i)
			{
				//��Ҫȥִ��һ���̺߳���
				threads_[i]->start();
				//��¼��ʼ�����̵߳�����
				idleThreadSize_++;
			}
		}
		//ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
		//pool.submitTask(sum1,10,20);csdn ���ؿ��� ��ֵ����+�����۵�ԭ��
		//Result submitTask(std::shared_ptr<Task> sp);
		template<typename Func,typename... Args>
		auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
		{
			//������񣬷ŵ��������
			using RType = decltype(func(args...));
			auto task = std::make_shared<std::packaged_task<RType()>>(
				std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
			std::future<RType> result = task->get_future();

			//��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			//�û��ύ�����������������1s,�����ж��ύ����ʧ��,����
			if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
			{
				//��ʾnotFull_�ȴ�1s�ӣ�������Ȼû������
				std::cout << "task queue is full,submit task fail." << std::endl;
				auto task = std::make_shared<std::packaged_task<RType()>>([]()->RType {
					return RType();
					});
				(*task)();
				return task->get_future();
			}
			//����п��࣬������������������
			//taskQue_.emplace(sp);

			taskQue_.emplace([task]() {
					//ȥִ�����������
				(*task)();
				});
			taskSize_++;

			//��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
			notEmpty_.notify_all();
			//fixedģʽ �ȽϺ�ʱ
			//cachedģʽ ������ȽϽ�����������С���������
			//��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
			if (poolMode_ == PoolMode::MODE_CACHED &&
				taskSize_ > idleThreadSize_ &&
				curThreadSize_ < threadSizeThreshHold_)
			{
				//�������߳�
				std::cout << " >>> create new thread.." << std::endl;
				auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
				int threadId = ptr->getId();
				threads_.emplace(threadId, std::move(ptr));
				threads_[threadId]->start();//�����߳�
				curThreadSize_++;
				idleThreadSize_++;
			}

			//���������Result����
			return result;
		}

	private:
		void threadFunc(int threadid)
		{
			auto lastTime = std::chrono::high_resolution_clock().now();
			while (isPoolRunning_)
			{
				Task task;
				{
					//�Ȼ�ȡ��
					std::unique_lock<std::mutex> lock(taskQueMtx_);
					//std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;
					//cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s,Ӧ�ðѶ�����߳�
					//�������յ�(����initThreadSize_�������߳�Ҫ���л���)
					//��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s

					//ÿһ���ӷ���һ�� ��ô���֣���ʱ����?�����������ִ�з���
					//��+˫���ж�
					while (isPoolRunning_ && taskQue_.size() == 0)
					{
						if (poolMode_ == PoolMode::MODE_CACHED)
						{
							//������������ʱ����
							if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
							{
								auto now = std::chrono::high_resolution_clock().now();
								auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
								if (dur.count() >= THREAD_MAX_IDLE_TIME &&
									curThreadSize_ > initThreadSize_)
								{
									//��ʼ���յ�ǰ�߳�
									//��¼�߳���������ر�����ֵ���޸�
									//���̶߳�����߳��б�������ɾ�� û�а취
									//threadid->thread����->ɾ��
									threads_.erase(threadid);
									curThreadSize_--;
									idleThreadSize_--;
									std::cout << "threadid:" << std::this_thread::get_id() << std::endl;
									return;
								}
							}
						}
						else
						{
							//�ȴ�notEmpty����
							notEmpty_.wait(lock);
						}
					}
					//�̳߳�Ҫ�����������߳���Դ
					if (!isPoolRunning_)
					{
						break;
					}

					idleThreadSize_--;
					std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�..." << std::endl;

					//�����������ȡ��һ���������
					task = taskQue_.front();
					taskQue_.pop();
					taskSize_--;
					//�����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
					if (taskQue_.size() > 0)
					{
						notEmpty_.notify_all();
					}

					//ȡ��һ�����񣬽���֪ͨ��֪ͨ���Լ����ύ��������
					notFull_.notify_all();
				}//��Ӧ�ð����ͷŵ�

				//��ǰ�̸߳���ִ���������
				if (task != nullptr)
				{
					//task->run();//ִ������;������ķ���ֵsetVal��������Result
					task();
				}

				idleThreadSize_++;
				//�����߳�ִ���������ʱ��
				lastTime = std::chrono::high_resolution_clock().now();
			}
			threads_.erase(threadid);
			std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
			exitCond_.notify_all();
		}

	private:
		std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�
		int initThreadSize_;//��ʼ���߳�����
		int threadSizeThreshHold_;//�߳�����������ֵ
		std::atomic_int curThreadSize_;//��¼��ǰ�̳߳������̵߳�������
		std::atomic_int idleThreadSize_;//��¼�����̵߳�����
		using Task = std::function<void()>;//�м�� void()
		std::queue<Task> taskQue_;//�������
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
