#include <iostream>
#include "skiplist.h"
#include <thread> // for this_thread
#define FILE_PATH "./store/dumpFile"

int main() {

    // 键值中的key用int型，如果用其他类型，需要自定义比较函数
    // 而且如果修改key的类型，同时需要修改skipList.load_file函数
    SkipList<int, std::string> skipList(6, std::chrono::seconds(2), 100);
	skipList.insert_element(1, "aaa"); 
	skipList.insert_element(3, "bbb"); 
	skipList.insert_element(3, "ccc"); 
	skipList.insert_element(3, "ddd"); 
	skipList.insert_element(5, "eee"); 
	skipList.insert_element(6, "fff"); 
	skipList.insert_element(7, "ggg"); 

    std::cout << "skipList size:" << skipList.size() << std::endl;

    skipList.dump_file();

    // skipList.load_file();

    skipList.search_element(3);
    skipList.search_element(2);

    skipList.display_list();

    skipList.delete_element(5);
    skipList.delete_element(7);
    std::cout << "skipList size:" << skipList.size() << std::endl;
    skipList.display_list();
    
    std::this_thread::sleep_for(std::chrono::seconds(3));
	skipList.insert_element(6, "hhh"); 
	skipList.insert_element(9, "hhh"); 
    skipList.search_element(3);
    skipList.search_element(9);
    std::cout << "skipList size:" << skipList.size() << std::endl;
    skipList.display_list();
}
