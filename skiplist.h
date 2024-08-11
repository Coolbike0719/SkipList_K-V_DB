#include <iostream> 
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <mutex>
#include <fstream>
#include <list>
#include <unordered_map>
#include <vector>
#include <unistd.h> 

#define STORE_FILE "store/dumpFile"

std::mutex mtx;
std::string delimiter = ":";
// 基于哈希表和双向链表的LRU缓存
template<typename K, typename V>
class LRUCache {
public:
    LRUCache(int capacity) : _capacity(capacity) {}

    // 获取值
    V get(const K& key) {
        if (cache_map.find(key) == cache_map.end()) {
            // 如果找不到，返回默认值
            return V();
        }
        // 更新节点位置
        move_to_front(key);
        return cache_map[key]->second;
    }

    // 插入值
    void put(const K& key, const V& value) {
        if (cache_map.find(key) != cache_map.end()) {
            // 更新现有值
            cache_map[key]->second = value;
            move_to_front(key);
        } else {
            if (cache_list.size() >= _capacity) {
                // 删除最旧的节点
                const K& old_key = cache_list.back().first;
                cache_list.pop_back();
                cache_map.erase(old_key);
            }
            // 插入新节点
            cache_list.emplace_front(key, value);
            cache_map[key] = cache_list.begin();
        }
    }
    
	void clear() {
	    cache_list.clear();
	    cache_map.clear();
	}
	
private:
    void move_to_front(const K& key) {
        // 将节点移动到前面
        auto it = cache_map[key];
        cache_list.splice(cache_list.begin(), cache_list, it);
    }

    int _capacity;
    std::list<std::pair<K, V>> cache_list; // 双向链表
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> cache_map; // 哈希表
};

// 具体用于存储的节点，被跳表类组织和管理
template<typename K, typename V>
class Node {
public:

    Node() {}
    Node(K k, V v, int);
    ~Node();

    K get_key() const;
    V get_value() const;
    void set_value(V);

    Node<K, V>** forward; // 二级指针，指向一个指针数组，存储该节点不同层级的后继节点(指针)
    int node_level; // 该节点出现的最高层级，即指针数组的最大下标，如forward[1]指向第二层的后继节点
    std::chrono::steady_clock::time_point last_time;  // 记录最后访问时间

private:
    K key;
    V value;
};

template<typename K, typename V>
Node<K, V>::Node(const K k, const V v, int level)
    : key(k), value(v), node_level(level), last_time(std::chrono::steady_clock::now()) {

    this->forward = new Node<K, V>* [level + 1];
    memset(this->forward, 0, sizeof(Node<K, V>*) * (level + 1));
}

template<typename K, typename V>
Node<K, V>::~Node() {
    delete[] forward;
};

template<typename K, typename V>
K Node<K, V>::get_key() const {
    return key;
};

template<typename K, typename V>
V Node<K, V>::get_value() const {
    return value;
};
template<typename K, typename V>
void Node<K, V>::set_value(V value) {
    this->value = value;
};

// 跳表类，组织管理Node
template <typename K, typename V>
class SkipList {

public:
    SkipList(int max_level, std::chrono::seconds expiration_duration, int cache_capacity);
    ~SkipList();
    int get_random_level();             // 获取节点的随机层级
    Node<K, V>* create_node(K, V, int); // 节点创建
    int insert_element(K, V);           // 插入节点
    void display_list();                // 显示跳表数据
    bool search_element(K);             // 搜索节点
    void delete_element(K);             // 删除节点
    void dump_file();                   // 持久化数据到文件
    void load_file();                   // 从文件加载数据
    void clear(Node<K, V>*);            // 递归删除节点
    int size();                         // 返回跳表中的节点个数

    bool is_expired(Node<K, V>*);       // 判断是否过期
    void cleanup_expired();             // 清理过期节点

private:
    void get_key_value_from_string(const std::string& str, std::string* key, std::string* value);
    bool is_valid_string(const std::string& str);

    int _max_level;                 // 跳表允许的最大层级
    int _skip_list_level;           // 跳表当前的最大层级
    Node<K, V>* _header;            // 跳表的头节点，即数据链表的1号节点
    int _element_count;             // 跳表中的节点个数
    std::ofstream _file_writer;     // 文件写入流
    std::ifstream _file_reader;     // 文件读取流
    std::unordered_map<K, Node<K, V>*> cache;  // 缓存键到节点的映射
    std::chrono::seconds _expiration_duration;  // 过期时间
    LRUCache<K, V> _cache;		// LRU缓存
};

// 创建新节点
template<typename K, typename V>
Node<K, V>* SkipList<K, V>::create_node(const K k, const V v, int level) {
    Node<K, V>* n = new Node<K, V>(k, v, level);
    return n;
}

// 插入数据到跳表中
template<typename K, typename V>
int SkipList<K, V>::insert_element(const K key, const V value) {

    Node<K, V>* current = this->_header;
    // 创建前驱节点数组，用于记录插入节点的前驱节点以更新关系
    Node<K, V>* update[_max_level + 1];
    memset(update, 0, sizeof(Node<K, V>*) * (_max_level + 1));

    mtx.lock();
    // 从当前最大层级开始找待插入节点在每层的前驱节点
    for (int i = _skip_list_level; i >= 0; i--) {
        // 寻找当前层中最接近且小于 key 的节点
        while (current->forward[i] != NULL && current->forward[i]->get_key() < key) {
            current = current->forward[i]; // 移动到下一节点
        }
        // 保存每层中最接近且小于插入节点的节点，即插入节点的前驱节点，以便后续插入时更新指针关系
        update[i] = current; // 虽然在这一过程高于插入节点层级的前驱结点也会记录(严格说不算前驱节点)，但后续并不会使用到
    }

    // current指针移动到最底层前驱节点的下一节点，即待插入位置
    current = current->forward[0];

    // 检查待插入的节点的键是否已存在，可加入修改操作
    if (current != NULL && current->get_key() == key) {
	current->last_time = std::chrono::steady_clock::now(); // 更新该已存在节点的访问时间
	_cache.put(key, current->get_value()); // 重新放入LRU队列(因为已有会置于队首)
	mtx.unlock();
	return 1;
    }

    // 若待插入节点不存在(即current指向末尾)或current指向节点不等于待插入节点(即应当在current之前插入节点)
    // 则应当在update[0]和当前节点之间插入节点
    if (current == NULL || current->get_key() != key) {

        // 通过随机函数决定新节点的层级高度
        int random_level = get_random_level();

        // 如果新节点的层级超出了跳表的当前最高层级，则更新跳表的最高层级
        if (random_level > _skip_list_level) {
            // 从原本的最高层级开始更新所有更高层级
            for (int i = _skip_list_level + 1; i < random_level + 1; i++) {
                update[i] = _header; // 将跳表头节点设置为当前节点前驱节点(这些新高层级目前只有头节点和当前节点)
            }
            _skip_list_level = random_level;
        }

        Node<K, V>* inserted_node = create_node(key, value, random_level);

        // 在原本的各层插入新节点指针，更新前后节点的关系，即插入到update数组中每个节点的后面
        for (int i = 0; i <= random_level; i++) {
            inserted_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = inserted_node;
        }
        cache[key] = inserted_node;  // 更新缓存哈希表
        _cache.put(key, value); // 插入缓存
        std::cout << "Inserted key:" << key << ", value:" << value << std::endl;
        _element_count++;
    }

    mtx.unlock();
    return 0;
}

// 显示跳表中的数据
template<typename K, typename V>
void SkipList<K, V>::display_list() {

    std::cout << "\n*****Skip List*****" << "\n";
    for (int i = 0; i <= _skip_list_level; i++) {
        Node<K, V>* node = this->_header->forward[i]; // 从底往高逐层显示数据
        std::cout << "Level " << i << ": ";
        while (node != NULL) {
            std::cout << node->get_key() << ":" << node->get_value() << ";";
            node = node->forward[i];
        }
        std::cout << std::endl;
    }
}

// 将内存中的数据转存到文件
template<typename K, typename V>
void SkipList<K, V>::dump_file() {

    std::cout << "dump_file-----------------" << std::endl;
    _file_writer.open(STORE_FILE);
    Node<K, V>* node = this->_header->forward[0];

    while (node != NULL) {
        _file_writer << node->get_key() << ":" << node->get_value() << "\n";
        std::cout << node->get_key() << ":" << node->get_value() << ";\n";
        node = node->forward[0]; // 移动到下一个数据节点
    }

    _file_writer.flush(); // 刷新流，将缓冲区内容写进磁盘文件
    _file_writer.close();
    return;
}

// 加载磁盘中的数据
template<typename K, typename V>
void SkipList<K, V>::load_file() {

    _file_reader.open(STORE_FILE);
    std::cout << "load_file-----------------" << std::endl;
    std::string line;
    std::string* key = new std::string();
    std::string* value = new std::string();
    while (getline(_file_reader, line)) {
        get_key_value_from_string(line, key, value); // 逐行解析字符串
        if (key->empty() || value->empty()) {
            continue;
        }
        // 将键转化为整型
        insert_element(stoi(*key), *value);
        std::cout << "key:" << *key << "value:" << *value << std::endl;
    }
    delete key;
    delete value;
    _file_reader.close();
}

// 获取当前跳表中的节点个数
template<typename K, typename V>
int SkipList<K, V>::size() {
    return _element_count;
}

// 解析字符串获取键值对
template<typename K, typename V>
void SkipList<K, V>::get_key_value_from_string(const std::string& str, std::string* key, std::string* value) {

    if (!is_valid_string(str)) { // 无效字符串直接返回
        return;
    }
    *key = str.substr(0, str.find(delimiter));
    *value = str.substr(str.find(delimiter) + 1, str.length());
}
// 判断是否为合法字符串
template<typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string& str) {

    if (str.empty()) {
        return false;
    }
    // 如果没有找到分隔符则为无效字符串
    if (str.find(delimiter) == std::string::npos) {
        return false;
    }
    return true;
}

// 删除跳表中的数据
template<typename K, typename V>
void SkipList<K, V>::delete_element(K key) {

    Node<K, V>* current = this->_header;
    Node<K, V>* update[_max_level + 1]; // 记录删除节点的前驱节点以更新链表
    memset(update, 0, sizeof(Node<K, V>*) * (_max_level + 1));
    mtx.lock();
    // 从当前最大层级开始找待删除节点在每层的前驱节点
    // 实际上是不超过删除节点的最大节点，高于删除节点层级的节点也会被记录但不会使用，只会操作真正的前驱节点
    for (int i = _skip_list_level; i >= 0; i--) {
        while (current->forward[i] != NULL && current->forward[i]->get_key() < key) {
            current = current->forward[i];
        }
        update[i] = current;
    }

    // current指针移动到最底层前驱节点的下一节点，即待删除节点(如果存在的话)
    current = current->forward[0];

    if (current != NULL && current->get_key() == key) {
        // 从底向上开始对该结点所有层级的前驱结点更新
        for (int i = 0; i <= _skip_list_level; i++) {
            // 当出现某层的后继结点不是该结点时跳出，因为再往上也都不会是了
            if (update[i]->forward[i] != current) break;

            update[i]->forward[i] = current->forward[i];
        }

        // 删除没有结点的层级，即在删除前仅有头结点(1号结点)和删除结点的层级，故必然是之前的最高层级开始
        while (_skip_list_level > 0 && _header->forward[_skip_list_level] == NULL) {
            _skip_list_level--;
        }
        cache.erase(key);
        delete current; // 真正删除释放结点
        _element_count--;
        std::cout << "Deleted key " << key << std::endl;
    }
    mtx.unlock();
    return;
}

// 搜索跳表中的数据
template<typename K, typename V>
bool SkipList<K, V>::search_element(K key) {
    std::cout << "search_element-----------------" << std::endl;
    auto cached_value = _cache.get(key);
    if (cached_value.empty()) {
	// 缓存里未找到，需从跳表中查找
	Node<K, V>* current = _header;

	// 从当前最大层级开始找目标节点在每层的前驱节点
	for (int i = _skip_list_level; i >= 0; i--) {
	while (current->forward[i] && current->forward[i]->get_key() < key) {
	    current = current->forward[i];
	}
	}

	// current指针移动到最底层前驱节点的下一节点，即目标节点应该在的位置(如果存在的话)
	current = current->forward[0];

	if (current and current->get_key() == key) {
	if (is_expired(current)) { // 虽然存在但是过期了，也当作删除失败
	    delete_element(key);
	    return false;
	}

	current->last_time = std::chrono::steady_clock::now();  // 更新访问时间
	std::cout << "Found key: " << key << ", value: " << current->get_value() << std::endl;
	return true;
	}

	std::cout << "Not Found Key:" << key << std::endl;

	return false;
    }
    
    return true; // 直接在缓存里命中了
}

// 跳表构造函数
template<typename K, typename V>
SkipList<K, V>::SkipList(int max_level, std::chrono::seconds expiration_duration, int cache_capacity)
	: _max_level(max_level), _skip_list_level(0), _element_count(0), _expiration_duration(expiration_duration),
	  _cache(cache_capacity) {
	K k;
	V v;
	_header = new Node<K, V>(k, v, _max_level);
}

// 跳表析构函数
template<typename K, typename V>
SkipList<K, V>::~SkipList() {
    // 关闭打开的文件
    if (_file_writer.is_open()) {
        _file_writer.close();
    }
    if (_file_reader.is_open()) {
        _file_reader.close();
    }
    _cache.clear();
    //递归删除跳表链条
    if (_header->forward[0] != nullptr) {
        clear(_header->forward[0]);
    }
    delete _header;

}
template <typename K, typename V>
void SkipList<K, V>::clear(Node<K, V>* cur) {
    if (cur->forward[0] != nullptr) {
        clear(cur->forward[0]);
    }
    delete(cur);
}

// 获取随机层级
template<typename K, typename V>
int SkipList<K, V>::get_random_level() {
    // 每个节点至少出现在第一层。
    int k = 1;
    // 使用 rand() % 2 随机决定是否升层，结果应呈现几何分布(不考虑最大层数的限制)
    while (rand() % 2) {
        k++;
    }
    // 确保节点层级不超过跳表最大值
    k = (k < this->_max_level) ? k : _max_level;

    return k;
};

// 判断节点是否过期
template<typename K, typename V>
bool SkipList<K, V>::is_expired(Node<K, V>* node) {
    if (node == nullptr) {
        return false;
    }
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - node->last_time);

    return age >= _expiration_duration;
}

// 定时清理过期节点
template<typename K, typename V>
void SkipList<K, V>::cleanup_expired() {
    std::vector<K> keys_to_remove;
    auto now = std::chrono::steady_clock::now();

    mtx.lock();
    for (const auto& item : cache) {
        const auto& key = item.first;
        const auto& node = item.second;
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - node->last_time);
        if (age >= _expiration_duration) {
            keys_to_remove.push_back(key);
        }
    }
    mtx.unlock();

    for (const auto& key : keys_to_remove) {
        delete_element(key);
    }
}
