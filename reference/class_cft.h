/* 2453712 刘睿霖 大数据 */
#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <climits>
#include <cfloat>
/* 允许按需加入需要的头文件等 */
using namespace std;

static const int MAX_LINE = 1024;			// 指定配置文件一行的最大长度，如果超过，则认为非法的配置文件
static const int MAX_STRLEN = 128;			// 指定以char数组方式读取时的最大长度
static const char *const SIMPLE_GNAME = ""; // 简单配置的组名

/* 指定各种类型数据的缺省值 */
static const char DEFAULT_CHAR_VALUE = '\0';
static const int DEFAULT_INT_VALUE = INT_MIN;
static const double DEFAULT_DOUBLE_VALUE = DBL_MIN;
static const char *DEFAULT_CSTRING_VALUE = "\0"; // 考虑到实参是数组/动态申请空间，不用nullptr
static const string DEFAULT_STRING_VALUE = "";
static const unsigned int DEFAULT_IPADDR_VALUE = 0;

/* 执行 项目名 = 值 形式的配置项的分隔符，常见的一般有两种，=或空格（含tab），如果有需要，可以扩充此处 */
enum class BREAK_CTYPE
{
	Equal, // 用 = 进行分隔，例 prompt = yes
	Space  // 用空格/tab进行分隔，例 prompt yes
};

enum CFGITEM_TYPE
{
	TYPE_INT = 0,	// 4字节整型
	TYPE_DOUBLE,	// 8字节浮点
	TYPE_STRING,	// 以string形式存放的字符串
	TYPE_CSTRING,	// 以char数组形式存放的字符串，最长不超过127个字符（MAX_STRLEN-1）
	TYPE_CHARACTER, // 取单个字符
	TYPE_IPADDR,	// 取IPv4(x.x.x.x)形式的IP地址，转为32位无符号整数表示
	TYPE_NULL		// 用于判断enum结束的值
};

/* 允许自定义其它需要的内容 */
struct item
{
	string item_name;  // 项名
	string item_value; // 项值
};

struct CFG_GROUP
{
	string group_name;	  // 组名
	vector<string> items; // 该组下的所有项，已经完成处理的项名/项值对
	CFG_GROUP &operator+=(const CFG_GROUP &other)
	{
		this->items.insert(this->items.end(), other.items.begin(), other.items.end()); // 将other的items追加到当前对象的items末尾
		return *this;
	}
};

/* 定义整个读配置文件类 */
class config_file_tools
{
private:
	string cfgname;
	enum BREAK_CTYPE item_separate_character_type; // 用于配置项名字和值的分隔符（例：分隔符"="，xxx=yyy /分隔符" \t"，xxx yyy或xxx\tyyy均可）

	/* 允许添加其它需要的数据成员和成员函数（private部分仅允许自用而不对外） */
	bool read_succeeded;																																				 // 标记是否成功读取配置文件并建立了cfg_list
	vector<CFG_GROUP> cfg_list;																																			 // 存放配置文件中所有组及其项的自定义结构体vector
	int find_group_index(const string &group_name, const bool is_case_sensitive = false, const size_t start = 0, const size_t count_param = 0);							 // 查找组名对应的索引，找不到返回-1，支持范围查找
	int find_item_index(const vector<item> &items, const string &item_name, const bool is_case_sensitive = false, const size_t start = 0, const size_t count_param = 0); // 查找项名对应的索引，找不到返回-1
	bool parse_item(const string &raw_item, string &item_name, string &item_value);																						 // 解析某项的原始内容，得到项名和项值
	int parse_group(const string &group_name, const bool is_case_sensitive, vector<item> &ret);																			 // 分析某组的所有项，返回在ret中。不对项去重。
	bool get_value(string &item_value, const string &group_name, const string &item_name, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	// 由于项的解析不在构造对象时完成，对象数据成员中没有存储项名和项值的结构，因此下面这个函数暂时不实现
	// string where_item(const string& item_name, const bool is_case_sensitive = false);

public:
	/* 构造函数的作用：将指定配置文件中的所有组/项均读入并存储到自定义结构中，方便后续各种get函数的调用 */
	config_file_tools(const char *const cfgname, const enum BREAK_CTYPE bctype = BREAK_CTYPE::Equal);
	config_file_tools(const string &cfgname, const enum BREAK_CTYPE bctype = BREAK_CTYPE::Equal);
	/* 如果有动态内存申请，则析构函数中记得释放空间 */
	virtual ~config_file_tools();

	/* 判断读配置文件是否成功（是否建立了cfg_list） */
	bool is_read_succeeded() const;

	/* 取配置文件中的所有组，返回在 vector 中 */
	int get_all_group(vector<string> &ret);

	/* 取配置文件中的某个组的所有项的原始内容，返回在 vector 中，默认是组名大小写不敏感 */
	int get_all_item(const char *const group_name, vector<string> &ret, const bool is_case_sensitive = false);
	int get_all_item(const string &group_name, vector<string> &ret, const bool is_case_sensitive = false);

	/* 取配置文件中的某个组的某项的原始内容，返回为string形式的原始内容（必须是含分隔符的项，否则没法匹配项目名），默认是组名/项目名大小写不敏感*/
	int item_get_raw(const char *const group_name, const char *const item_name, string &ret, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_raw(const string &group_name, const string &item_name, string &ret, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 仅检查配置文件中的某个组的某项的项名是否存在，存在返回1，不存在返回0，不返回具体的值（必须是含分隔符的项，否则没法匹配项目名），默认是组名/项目名大小写不敏感*/
	int item_get_null(const char *const group_name, const char *const item_name, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_null(const string &group_name, const string &item_name, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 用于读指定类型的配置项的函数 - char */
	int item_get_char(const char *const group_name, const char *const item_name, char &value,
					  const char *const choice_set = nullptr, const char def_value = DEFAULT_CHAR_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_char(const string &group_name, const string &item_name, char &value,
					  const char *const choice_set = nullptr, const char def_value = DEFAULT_CHAR_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 用于读指定类型的配置项的函数 - int */
	int item_get_int(const char *const group_name, const char *const item_name, int &value,
					 const int min_value = INT_MIN, const int max_value = INT_MAX, const int def_value = DEFAULT_INT_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_int(const string &group_name, const string &item_name, int &value,
					 const int min_value = INT_MIN, const int max_value = INT_MAX, const int def_value = DEFAULT_INT_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 用于读指定类型的配置项的函数 - double */
	int item_get_double(const char *const group_name, const char *const item_name, double &value,
						const double min_value = DBL_MIN, const double max_value = DBL_MAX, const double def_value = DEFAULT_DOUBLE_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_double(const string &group_name, const string &item_name, double &value,
						const double min_value = DBL_MIN, const double max_value = DBL_MAX, const double def_value = DEFAULT_DOUBLE_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 用于读指定类型的配置项的函数 - char* / char[] */
	int item_get_cstring(const char *const group_name, const char *const item_name, char *const value,
						 const int str_maxlen, const char *const def_value = DEFAULT_CSTRING_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_cstring(const string &group_name, const string &item_name, char *const value,
						 const int str_maxlen, const char *const def_value = DEFAULT_CSTRING_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 用于读指定类型的配置项的函数 - string */
	int item_get_string(const char *const group_name, const char *const item_name, string &value,
						const string &def_value = DEFAULT_STRING_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_string(const string &group_name, const string &item_name, string &value,
						const string &def_value = DEFAULT_STRING_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 用于读指定类型的配置项的函数 - IP地址 */
	int item_get_ipaddr(const char *const group_name, const char *const item_name, unsigned int &value,
						const unsigned int &def_value = DEFAULT_IPADDR_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);
	int item_get_ipaddr(const string &group_name, const string &item_name, unsigned int &value,
						const unsigned int &def_value = DEFAULT_IPADDR_VALUE, const bool group_is_case_sensitive = false, const bool item_is_case_sensitive = false);

	/* 允许按需加入需要的友元声明等，public中不再允许放入其它成员函数（因为外部无调用需求）*/
};
