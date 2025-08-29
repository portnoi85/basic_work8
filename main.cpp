#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>


#include "CRC32.hpp"
#include "IO.hpp"

struct CalcData {
  size_t begin;
  size_t end;
  uint32_t originalCrc32;
  uint32_t badCrc32;
  std::atomic_uint64_t &result;
};

/// @brief Переписывает последние 4 байта значением value
void replaceLastFourBytes(std::vector<char> &data, uint32_t value) {
  std::copy_n(reinterpret_cast<const char *>(&value), 4, data.end() - 4);
}

void findcrc(CalcData data, std::mutex *cout_mutex) {
  std::vector<char> buff(4);
  for (uint64_t i = data.begin; i < data.end; ++i) {
    // Заменяем четыре байта переменной buff на значение i
    replaceLastFourBytes(buff, uint32_t(i));
    // Вычисляем CRC32 с учетом ранее рассчитанного значения.
    auto currentCrc32 = crc32(buff.data(), 4, data.badCrc32);
    if (currentCrc32 == data.originalCrc32) {
      data.result = i;
      return;
    }
    // Отображаем прогресс
    if (i % 10000000 == 0) {
      if (data.result != UINT64_MAX)
        return;
      std::lock_guard<std::mutex> guard(*cout_mutex);
      std::cout << "progress[" << std::this_thread::get_id() << "]: "
                << static_cast<double>(i - data.begin) / static_cast<double>(data.end - data.begin)
                << std::endl;
    }
  }
  return;
}

unsigned int optimization(CalcData data, std::mutex *cout_mutex) {
  bool findTreadCount = true;
  unsigned int t = 0;
  std::chrono::duration<double> calcPrev;
  while ( (data.result == UINT64_MAX) && (findTreadCount)) {
    std::vector<std::thread> threads;
    ++t;
    std::cout << "count threads = " << t << std::endl;
    auto start = std::chrono::steady_clock::now();
    for (size_t j = 0; j < t ; ++j) {
      data.begin = t == 1 ? 0 : data.begin + 30000000;
      data.end = data.begin + 30000000;
      threads.emplace_back(findcrc, data, cout_mutex);
    }
    for (auto &t : threads) {
      if (t.joinable()) {
        t.join();
      }
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> calcTime = (end - start) / t; //время, необходимое для провреки 3e7 значений
    std::cout << "time(" << t << " threads) = " << calcTime.count() << std::endl;;
    if ((t > 1) && (calcPrev < calcTime)) {
      findTreadCount = false;
      --t;
      std::cout << "optimal count threads = " << t << std::endl;;
    } else {
      calcPrev = calcTime;
    }
  }

  return t;
}

/**
 * @brief Формирует новый вектор с тем же CRC32, добавляя в конец оригинального
 * строку injection и дополнительные 4 байта
 * @details При формировании нового вектора последние 4 байта не несут полезной
 * нагрузки и подбираются таким образом, чтобы CRC32 нового и оригинального
 * вектора совпадали
 * @param original оригинальный вектор
 * @param injection произвольная строка, которая будет добавлена после данных
 * оригинального вектора
 * @return новый вектор
 */
std::vector<char> hack(const std::vector<char> &original,
                       const std::string &injection) {
  std::vector<char> result(original.size() + injection.size() + 4);
  auto it = std::copy(original.begin(), original.end(), result.begin());
  std::copy(injection.begin(), injection.end(), it);

  const uint32_t originalCrc32 = crc32(original.data(), original.size());
  const uint32_t badCrc32 = ~crc32(injection.data(), injection.size(), ~originalCrc32);
  std::mutex cout_mutex;
  const size_t maxVal = std::numeric_limits<uint32_t>::max();
  
  std::atomic_uint64_t hack_val(UINT64_MAX);
  std::vector<std::thread> threads;

  CalcData data {0, 0, originalCrc32, badCrc32, hack_val};
  
  unsigned int t = optimization(data, &cout_mutex);
  if (hack_val != UINT64_MAX) {
    std::cout << "Success\n";
    replaceLastFourBytes(result, static_cast<uint32_t>(hack_val));
    return result;
  }

  size_t MinVal =data.end;
  size_t step = (maxVal - MinVal) / t;
  for (size_t j = 0; j < t ; ++j) {
    data.begin = MinVal + step * j;
    data.end = (j + 1 < t ) ? (MinVal + step * (j + 1)) : maxVal;
    threads.emplace_back(findcrc, data, &cout_mutex);
  }
  for (auto &t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  if (hack_val != UINT64_MAX) {
    std::cout << "Success\n";
    replaceLastFourBytes(result, static_cast<uint32_t>(hack_val));
    return result;
  }
  throw std::logic_error("Can't hack");
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Call with two args: " << argv[0]
              << " <input file> <output file>\n";
    return 1;
  }
  auto start = std::chrono::steady_clock::now();
  try {
    const std::vector<char> data = readFromFile(argv[1]);
    const std::vector<char> badData = hack(data, "He-he-he");
    writeToFile(argv[2], badData);
  } catch (std::exception &ex) {
    std::cerr << ex.what() << '\n';
    return 2;
  }
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "time= " << elapsed.count() * 2 << std::endl;
  return 0;
}
