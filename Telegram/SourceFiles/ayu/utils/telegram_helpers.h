// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

using ID = uint64;

class HistoryItem;
namespace Data {
class Media;
} // namespace Data
struct TextWithTags;

[[nodiscard]] TextWithTags extractText(not_null<HistoryItem*> item);
[[nodiscard]] bool mediaDownloadable(const Data::Media *media);
[[nodiscard]] bool exdIsDeleted(not_null<const HistoryItem*> item);

class TimedCountDownLatch
{
public:
    explicit TimedCountDownLatch(int count)
        : count_(count) {
    }

    TimedCountDownLatch(const TimedCountDownLatch &) = delete;
    TimedCountDownLatch &operator=(const TimedCountDownLatch &) = delete;

    void countDown() {
        std::unique_lock lock(mutex_);
        if (count_ > 0) {
            count_--;
        }
        if (count_ == 0) {
            cv_.notify_all();
        }
    }

    bool await(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (count_ == 0) {
            return true;
        }
        return cv_.wait_for(lock, timeout, [this] { return count_ == 0; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
};

inline Main::Session *getSession(ID userId) {
	if (!userId) {
		return nullptr;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			if (session->userId().bare == userId) {
				return session;
			}
		}
	}
	return nullptr;
}

inline void dispatchToMainThread(const std::function<void()> &callback, int delay = 0) {
	auto timer = new QTimer();
	timer->moveToThread(qApp->thread());
	timer->setSingleShot(true);
	QObject::connect(timer,
					 &QTimer::timeout,
					 [=]()
					 {
						 callback();
						 timer->deleteLater();
					 });
	QMetaObject::invokeMethod(timer, "start", Qt::QueuedConnection, Q_ARG(int, delay));
}
