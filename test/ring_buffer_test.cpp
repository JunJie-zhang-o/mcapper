#include <cassert>
#include <vector>

#include <mcapper/ring_buffer.hpp>

int main() {
  mcapper::RingBuffer<int> buffer(3);
  buffer.push(1);
  buffer.push(2);
  buffer.push(3);

  auto snapshot = buffer.snapshot();
  assert((snapshot == std::vector<int>{1, 2, 3}));

  buffer.push(4);
  snapshot = buffer.snapshot();
  assert((snapshot == std::vector<int>{2, 3, 4}));

  int value = 0;
  assert(buffer.pop(value));
  assert(value == 2);
  assert(buffer.pop(value));
  assert(value == 3);
  assert(buffer.pop(value));
  assert(value == 4);
  assert(!buffer.pop(value));

  return 0;
}
