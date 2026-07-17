#include "engine.hpp"
#include <iostream>
#include <cassert>

int main() {
    {
        LSMEngine engine("./data", /*flush_threshold_bytes=*/40);

        engine.Put("apple", "red");
        engine.Put("banana", "yellow");
        engine.Put("cherry", "dark red");
        engine.Delete("banana");
        engine.Put("date", "brown");
        engine.Put("elderberry", "purple");
        engine.Put("fig", "purple-ish");
        engine.Put("grape", "green");
        engine.Put("honeydew", "pale green");

        std::cout << "apple: " << engine.Get("apple").value_or("<not found>") << "\n";
        std::cout << "banana: " << engine.Get("banana").value_or("<not found>") << "\n";
        std::cout << "grape: " << engine.Get("grape").value_or("<not found>") << "\n";
        std::cout << "unknown: " << engine.Get("unknown").value_or("<not found>") << "\n";

        std::cout << "memtable count: " << engine.MemTableCount() << "\n";
        std::cout << "L0 sstable count: " << engine.SSTableCountAtLevel(0) << "\n";
        std::cout << "L1 sstable count: " << engine.SSTableCountAtLevel(1) << "\n";

        assert(engine.Get("apple") == "red");
        assert(!engine.Get("banana").has_value());
        assert(!engine.Get("unknown").has_value());
    }

    {
        LSMEngine engine2("./data", 40);
        std::cout << "\n--- after restart ---\n";
        std::cout << "apple: " << engine2.Get("apple").value_or("<not found>") << "\n";
        std::cout << "cherry: " << engine2.Get("cherry").value_or("<not found>") << "\n";
        assert(engine2.Get("apple") == "red");
        assert(engine2.Get("cherry") == "dark red");
    }

    std::cout << "\nAll assertions passed.\n";
    return 0;
}