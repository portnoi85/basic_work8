#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>
#include <thread>
#include <mutex>

#include "CRC32.hpp"
#include "IO.hpp"

struct CalcData {
  size_t begin;
  size_t end;
  uint32_t originalCrc32;
  uint32_t badCrc32;
  bool &sucsess;
  std::vector<char> &result;
};

/// @brief Переписывает последние 4 байта значением value
void replaceLastFourBytes(std::vector<char> &data, uint32_t value) {
  std::copy_n(reinterpret_cast<const char *>(&value), 4, data.end() - 4);
}

void findcrc(CalcData data, std::mutex *cout_mutex) {
  std::vector<char> buff(4);
  buff.reserve(4);
  for (size_t i = data.begin; i < data.end; ++i) {
    // Заменяем четыре байта переменной buff на значение i
    replaceLastFourBytes(buff, uint32_t(i));
    // Вычисляем CRC32 с учетом ранее рассчитанного значения.
    auto currentCrc32 = crc32(buff.data(), 4, data.badCrc32);
    if (currentCrc32 == data.originalCrc32) {
      std::cout << "Success\n";
      data.sucsess = true;
      replaceLastFourBytes(data.result, uint32_t(i));
      return;
    }
    // Отображаем прогресс
    if (i % 10000000 == 0) {
      if (data.sucsess)
        return;
      std::lock_guard<std::mutex> guard(*cout_mutex);
      std::cout << "progress[" << std::this_thread::get_id() << "]: "
                << static_cast<double>(i - data.begin) / static_cast<double>(data.end - data.begin)
                << std::endl;
    }
  }
  return;
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
  const uint32_t badCrc32 = ~crc32(result.data(), result.size() - 4);
  std::mutex cout_mutex;
  const size_t maxVal = std::numeric_limits<uint32_t>::max();
  unsigned int t = std::thread::hardware_concurrency();
  bool sucsess = false;
  std::vector<std::thread> threads;
  threads.reserve(t);

  CalcData data {0, 0, originalCrc32, badCrc32, sucsess, result};
  size_t step = maxVal / t;
  for (size_t j = 0; j < t ; ++j) {
    data.begin = step * j;
    data.end = (j + 1 < t ) ? (step * (j + 1)) : maxVal;
    threads.emplace_back(findcrc, data, &cout_mutex);
  }
  for (auto &t : threads) {
    t.join();
  }
  if (sucsess) {
//    std::cout << "Success" << std::endl;
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

  try {
    const std::vector<char> data = readFromFile(argv[1]);
    const std::vector<char> badData = hack(data, "He-he-he");
    writeToFile(argv[2], badData);
  } catch (std::exception &ex) {
    std::cerr << ex.what() << '\n';
    return 2;
  }
  return 0;
}
