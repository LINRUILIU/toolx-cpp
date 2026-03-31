/* 2453712 刘睿霖 大数据 */
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
/* 添加自己需要的头文件，注意限制 */
#include <sstream>
#include <iomanip>
#include <cstring>
#include "../include/class_cft.h"
using namespace std;

/* 给出各种自定义函数的实现（已给出的内容不全） */

/* 工具函数的静态实现 */
static inline char tolower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// 两个对比string的函数，对标stricmp与strcmp
static bool sicmp(const string &a, const string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (tolower(a[i]) != tolower(b[i]))
			return false;
	}
	return true;
}

static bool scmp(const string &a, const string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i] != b[i])
			return false;
	}
	return true;
} // 等价于 return a == b;

// 返回第一个匹配的索引，未找到返回 -1
static int find_index(const vector<string> &vec, const string &key, bool case_sensitive = false, int start = 0)
{
	if (start < 0)
		start = 0;
	for (int i = start, n = static_cast<int>(vec.size()); i < n; ++i)
	{
		if (case_sensitive ? scmp(vec[i], key) : sicmp(vec[i], key))
			return i;
	}
	return -1;
}

// 是否包含
static bool contains(const vector<string> &vec, const string &key, bool case_sensitive = false)
{
	return find_index(vec, key, case_sensitive) >= 0;
}

template <typename T>
static bool try_parse_number(const string &text, T &value)
{
	stringstream ss;
	ss << text;
	ss >> value;
	return (!ss.fail() && ss.eof());
}

int config_file_tools::find_group_index(const string &group_name, const bool is_case_sensitive, const size_t start, const size_t count_param)
{
	size_t len = cfg_list.size();
	if (start >= len)
		return -1;
	size_t count = (count_param == 0 || start + count_param > len) ? len - start : count_param;
	for (size_t i = start, end = start + count; i < end && i < len; ++i)
	{
		const string &g = cfg_list[i].group_name;
		if (is_case_sensitive ? scmp(g, group_name) : sicmp(g, group_name))
			return static_cast<int>(i);
	}
	return -1;
}

int config_file_tools::find_item_index(const vector<item> &items, const string &item_name, const bool is_case_sensitive, const size_t start, const size_t count_param)
{
	size_t len = items.size();
	if (start >= len)
		return -1;
	size_t count = (count_param == 0 || start + count_param > len) ? len - start : count_param;
	for (size_t i = start, end = start + count; i < end && i < len; ++i)
	{
		const string &nm = items[i].item_name;
		if (is_case_sensitive ? scmp(nm, item_name) : sicmp(nm, item_name))
			return static_cast<int>(i);
	}
	return -1;
}

// 将字符串去掉前后空白字符
static void trim(string &s)
{
	if (s.empty())
		return;
	// 注意isspace的参数必须是unsigned char转换后的值，否则可能出错
	size_t start = 0;
	while (start < s.size() && isspace(static_cast<unsigned char>(s[start])))
		++start;
	size_t end = s.size();
	while (end > start && isspace(static_cast<unsigned char>(s[end - 1])))
		--end;
	if (end <= start)
	{
		s = "";
		return;
	}
	s = s.substr(start, end - start); // substr的第二个参数是长度
}

// 去除字符串中的注释部分（从注释符开始到行尾）
static void remove_comment(string &s)
{
	// 不需要特殊处理空字符串
	size_t pos_slash = s.find("//");
	size_t pos_hash_sem = s.find_first_of(";#");
	size_t pos = string::npos;

	if (pos_slash != string::npos && pos_hash_sem != string::npos)
		pos = (pos_slash < pos_hash_sem) ? pos_slash : pos_hash_sem;
	else if (pos_slash != string::npos)
		pos = pos_slash;
	else
		pos = pos_hash_sem;

	if (pos != string::npos)
		s = s.substr(0, pos); // 截取到注释符前面，可能为空字符串
}

static inline bool is_valid_ip_str(const string &ip)
{
	if (ip.empty())
		return false;

	unsigned int num = 0;
	int dots = 0;
	int seg_len = 0;
	const char *ptr = ip.c_str(); // 使用c方式的字符指针

	while (*ptr)
	{
		if (*ptr == '.')
		{
			if (seg_len == 0 || num > 255)
				return false;
			num = 0;
			seg_len = 0;
			++dots;
		}
		else if (*ptr >= '0' && *ptr <= '9')
		{
			if (++seg_len > 3)
				return false;
			num = num * 10 + (*ptr - '0');
		}
		else
			return false;
		++ptr;
	}
	// 必须有 3 个点，且最后一段也要非空且 <= 255
	if (dots != 3 || seg_len == 0 || num > 255)
		return false;
	return true;
}

static inline unsigned int ip_str_to_value(const string &ip) // 内部使用，使用前保证格式正确
{
	unsigned int a = 0, b = 0, c = 0, d = 0;
	char dot1 = 0, dot2 = 0, dot3 = 0;
	istringstream ss(ip);
	ss >> a >> dot1 >> b >> dot2 >> c >> dot3 >> d;

	return (static_cast<unsigned int>(a) << 24) |
		   (static_cast<unsigned int>(b) << 16) |
		   (static_cast<unsigned int>(c) << 8) |
		   static_cast<unsigned int>(d);
}

bool config_file_tools::parse_item(const string &raw_item, string &item_name, string &item_value)
{
	size_t pos = string::npos;
	if (item_separate_character_type == BREAK_CTYPE::Equal)
	{
		pos = raw_item.find('=');
	}
	else if (item_separate_character_type == BREAK_CTYPE::Space)
	{
		pos = raw_item.find_first_of(" \t");
	}
	if (pos == string::npos)
	{
		// 未找到按类中存储的指定分隔符类型的分隔符
		return false;
	}
	item_name = raw_item.substr(0, pos);
	item_value = raw_item.substr(pos + 1);
	trim(item_name);
	trim(item_value);
	return true;
}

int config_file_tools::parse_group(const string &group_name, const bool is_case_sensitive, vector<item> &ret)
{
	ret.clear();
	int group_index = find_group_index(group_name, is_case_sensitive);
	if (group_index < 0)
	{
		// 组不存在
		return 0;
	}
	CFG_GROUP group = cfg_list[static_cast<size_t>(group_index)];
	if (is_case_sensitive)
		while (group_index >= 0)
		{
			group_index = find_group_index(group_name, is_case_sensitive, static_cast<size_t>(group_index) + 1);
			if (group_index >= 0)
				group += cfg_list[static_cast<size_t>(group_index)]; // 组存在，合并项（不去重）
		} // while结束后group包含了所有同名组的项，由于构造cfg_list时组名已经去重，这里实际只处理case_sensitive为true的情况。
	string item_name, item_value;
	for (const string &raw_item : group.items)
	{
		if (parse_item(raw_item, item_name, item_value))
		{
			ret.push_back({item_name, item_value}); // 解析成功
		}
	}
	return static_cast<int>(ret.size());
}

bool config_file_tools::get_value(string &item_value, const string &group_name, const string &item_name, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	vector<item> items;
	int item_count = parse_group(group_name, group_is_case_sensitive, items);
	if (item_count <= 0)
	{
		// 组不存在或无项
		return false;
	}
	int item_index = find_item_index(items, item_name, item_is_case_sensitive);
	if (item_index < 0)
	{
		// 项不存在
		return false;
	}
	item_value = items[static_cast<size_t>(item_index)].item_value;
	return true;
}

/***************************************************************************
  函数名称：
  功    能：
  输入参数：
  返 回 值：
  说    明： 将名字转为string后调用string的重载使用
			 委托构造函数，避免临时无名string对象的析构问题
***************************************************************************/
config_file_tools::config_file_tools(const char *const cfgname, const enum BREAK_CTYPE bctype)
	: config_file_tools(string(cfgname), bctype) {}

/***************************************************************************
  函数名称：
  功    能：
  输入参数：
  返 回 值：
  说    明：
***************************************************************************/
config_file_tools::config_file_tools(const string &cfgname, const enum BREAK_CTYPE bctype)
{
	this->cfgname = cfgname;
	this->item_separate_character_type = bctype;
	this->read_succeeded = true; // 先假设成功

	ifstream fcfg;
	fcfg.open(cfgname, ios::in | ios::binary);
	if (!fcfg.is_open())
	{
		// 打开失败
		read_succeeded = false;
		return;
	}

	cfg_list.clear();
	CFG_GROUP default_group;
	default_group.group_name = SIMPLE_GNAME; // 简单配置的组名为空字符串
	cfg_list.push_back(default_group);

	size_t current_group_index = 0; // 当前组索引，初始为第一个组（简单配置的空组）
	while (!fcfg.eof())
	{
		string line;
		getline(fcfg, line);
		if (!line.empty() && line.back() == '\r')
			line.pop_back(); // 去除可能的\r字符（Windows下的文本文件）
		if (line.size() > MAX_LINE)
		{
			// 行过长
			read_succeeded = false;
			fcfg.close();
			return;
		}

		remove_comment(line); // 去除注释
		trim(line);			  // 去除前后空白
		if (line.empty())
			continue; // 空行跳过

		// 处理行内容
		if (line[0] == '[' && line.back() == ']')
		{
			// 组名行
			string group_name = line.substr(1, line.size() - 2);
			trim(group_name);
			group_name.insert(0, "[");
			group_name.push_back(']');
			// 新建组
			int group_index = find_group_index(group_name, true); // 组名大小写敏感查找
			if (group_index >= 0)
			{
				// 组已存在，继续使用该组
				current_group_index = static_cast<size_t>(group_index);
				continue;
			}
			CFG_GROUP new_group;
			new_group.group_name = group_name;
			cfg_list.push_back(new_group);
			current_group_index = cfg_list.size() - 1;
			continue;
		}
		// 配置项行
		cfg_list[current_group_index].items.push_back(line);
	}
	fcfg.close();
	if (cfg_list[0].items.size() == 0)
		cfg_list.erase(cfg_list.begin()); // 删除简单配置的空组
}

/***************************************************************************
  函数名称：
  功    能：
  输入参数：
  返 回 值：
  说    明：
***************************************************************************/
config_file_tools::~config_file_tools()
{
	/* 按需完成 */
}

/***************************************************************************
  函数名称：
  功    能：判断读配置文件是否成功
  输入参数：
  返 回 值：true - 成功，已读入所有的组/项
		   false - 失败，文件某行超长/文件全部是注释语句
  说    明：
***************************************************************************/
bool config_file_tools::is_read_succeeded() const
{
	return this->read_succeeded;
}

/***************************************************************************
  函数名称：
  功    能：返回配置文件中的所有组
  输入参数：vector <string>& ret : vector 中每项为一个组名
  返 回 值：读到的组的数量（简单配置文件的组数量为1，组名为"）
  说    明：
***************************************************************************/
int config_file_tools::get_all_group(vector<string> &ret)
{
	/* 按需完成，根据需要改变return的值 */
	ret.clear();
	for (int i = 0, n = static_cast<int>(cfg_list.size()); i < n; ++i)
		ret.push_back(cfg_list[i].group_name);
	return static_cast<int>(ret.size());
}

/***************************************************************************
  函数名称：
  功    能：查找指定组的所有项并返回项的原始内容
  输入参数：const char* const group_name：组名
		   vector <string>& ret：vector 中每项为一个项的原始内容
		   const bool is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：项的数量，0表示空
  说    明：
***************************************************************************/
int config_file_tools::get_all_item(const char *const group_name, vector<string> &ret, const bool is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	return this->get_all_item(string(group_name), ret, is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：
***************************************************************************/
int config_file_tools::get_all_item(const string &group_name, vector<string> &ret, const bool is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	int g_index = find_group_index(group_name, is_case_sensitive);
	if (g_index < 0)
		return 0; // 未找到组
	ret.clear();
	while (g_index >= 0)
	{
		for (int i = 0, n = static_cast<int>(cfg_list[g_index].items.size()); i < n; ++i)
			ret.push_back(cfg_list[g_index].items[i]);
		g_index = find_group_index(group_name, is_case_sensitive, static_cast<size_t>(g_index) + 1); // 查找下一个同名组
	}
	return static_cast<int>(ret.size());
}

/***************************************************************************
  函数名称：
  功    能：取某项的原始内容（=后的所有字符，string方式）
  输入参数：const char* const group_name
		   const char* const item_name
		   string &ret
		   const bool group_is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false  : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：
  说    明：取第一项匹配的原始内容
***************************************************************************/
int config_file_tools::item_get_raw(const char *const group_name, const char *const item_name, string &ret, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	if (group_name == nullptr || item_name == nullptr)
		return 0;
	return this->get_value(ret, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive) ? 1 : 0;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：
***************************************************************************/
int config_file_tools::item_get_raw(const string &group_name, const string &item_name, string &ret, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 本函数已实现 */
	return this->item_get_raw(group_name.c_str(), item_name.c_str(), ret, group_is_case_sensitive, item_is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：取某项的内容，返回类型为char型
  输入参数：const char* const group_name               ：组名
		   const char* const item_name                ：项名
		   const bool group_is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false  : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：1 - 该项的项名存在
		   0 - 该项的项名不存在
  说    明：
***************************************************************************/
int config_file_tools::item_get_null(const char *const group_name, const char *const item_name, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	if (group_name == nullptr || item_name == nullptr)
		return 0;
	string dummy;
	return this->get_value(dummy, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive) ? 1 : 0;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：因为工具函数一般在程序初始化阶段被调用，不会在程序执行中被高频次调用，
		   因此这里直接套壳，会略微影响效率，但不影响整体性能（对高频次调用，此方法不适合）
***************************************************************************/
int config_file_tools::item_get_null(const string &group_name, const string &item_name, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 本函数已实现 */
	return this->item_get_null(group_name.c_str(), item_name.c_str(), group_is_case_sensitive, item_is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：取某项的内容，返回类型为char型
  输入参数：const char* const group_name               ：组名
		   const char* const item_name                ：项名
		   char& value                                ：读到的char的值（返回1时可信，返回0则不可信）
		   const char* const choice_set = nullptr     ：合法的char的集合（例如："YyNn"表示合法输入为Y/N且不分大小写，该参数有默认值nullptr，表示全部字符，即不检查）
		   const char def_value = DEFAULT_CHAR_VALUE  ：读不到/读到非法的情况下的默认值，该参数有默认值DEFAULT_CHAR_VALUE，分两种情况
															当值是   DEFAULT_CHAR_VALUE 时，返回0（值不可信）
															当值不是 DEFAULT_CHAR_VALUE 时，令value=def_value并返回1
		   const bool group_is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false  : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：1 - 取到正确值
			   未取到值/未取到正确值，设置了缺省值（包括设为缺省值）
		   0 - 未取到（只有为未指定默认值的情况下才会返回0）
  说    明：
***************************************************************************/
int config_file_tools::item_get_char(const char *const group_name, const char *const item_name, char &value,
									 const char *const choice_set, const char def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	if (group_name == nullptr || item_name == nullptr)
	{
		if (def_value != DEFAULT_CHAR_VALUE)
		{
			value = def_value;
			return 1;
		}
		return 0;
	}
	string item_value;
	if (!this->get_value(item_value, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive))
	{
		// 未取到值
		if (def_value != DEFAULT_CHAR_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	char c = '\0';
	// 检查合法性
	bool is_valid = true;
	if (item_value.size() != 1)
		is_valid = false;
	else
		c = item_value[0];
	if (choice_set != nullptr)
	{
		string choices(choice_set);
		size_t choice_index = choices.find(c);
		if (choice_index == string::npos)
			is_valid = false;
	}
	if (!is_valid)
	{
		// 非法值
		if (def_value != DEFAULT_CHAR_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	// 合法值
	value = c;
	return 1;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：因为工具函数一般在程序初始化阶段被调用，不会在程序执行中被高频次调用，
		   因此这里直接套壳，会略微影响效率，但不影响整体性能（对高频次调用，此方法不适合）
***************************************************************************/
int config_file_tools::item_get_char(const string &group_name, const string &item_name, char &value,
									 const char *const choice_set, const char def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 本函数已实现 */
	return this->item_get_char(group_name.c_str(), item_name.c_str(), value, choice_set, def_value, group_is_case_sensitive, item_is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：取某项的内容，返回类型为int型
  输入参数：const char* const group_name               ：组名
		   const char* const item_name                ：项名
		   int& value                                 ：读到的int的值（返回1时可信，返回0则不可信）
		   const int min_value = INT_MIN              : 期望数据范围的下限，默认为INT_MIN
		   const int max_value = INT_MAX              : 期望数据范围的上限，默认为INT_MAX
		   const int def_value = DEFAULT_INT_VALUE    ：读不到/读到非法的情况下的默认值，该参数有默认值 DEFAULT_INT_VALUE，分两种情况
															当值是   DEFAULT_INT_VALUE 时，返回0（值不可信）
															当值不是 DEFAULT_INT_VALUE 时，令value=def_value并返回1
		   const bool group_is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false  : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：
  说    明：
***************************************************************************/
int config_file_tools::item_get_int(const char *const group_name, const char *const item_name, int &value,
									const int min_value, const int max_value, const int def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	if (group_name == nullptr || item_name == nullptr)
	{
		if (def_value != DEFAULT_INT_VALUE)
		{
			value = def_value;
			return 1;
		}
		return 0;
	}
	string item_value;
	if (!this->get_value(item_value, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive))
	{
		// 未取到值
		if (def_value != DEFAULT_INT_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	int v;
	if (!try_parse_number(item_value, v))
	{
		if (def_value != DEFAULT_INT_VALUE)
		{
			value = def_value;
			return 1;
		}
		return 0;
	}
	// 检查合法性
	if (v < min_value || v > max_value)
	{
		// 非法值
		if (def_value != DEFAULT_INT_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	// 合法值
	value = v;
	return 1;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：因为工具函数一般在程序初始化阶段被调用，不会在程序执行中被高频次调用，
		   因此这里直接套壳，会略微影响效率，但不影响整体性能（对高频次调用，此方法不适合）
***************************************************************************/
int config_file_tools::item_get_int(const string &group_name, const string &item_name, int &value,
									const int min_value, const int max_value, const int def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 本函数已实现 */
	return this->item_get_int(group_name.c_str(), item_name.c_str(), value, min_value, max_value, def_value, group_is_case_sensitive, item_is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：取某项的内容，返回类型为double型
  输入参数：const char* const group_name                  ：组名
		   const char* const item_name                   ：项名
		   double& value                                 ：读到的int的值（返回1时可信，返回0则不可信）
		   const double min_value = __DBL_MIN__          : 期望数据范围的下限，默认为INT_MIN
		   const double max_value = __DBL_MAX__          : 期望数据范围的上限，默认为INT_MAX
		   const double def_value = DEFAULT_DOUBLE_VALUE ：读不到/读到非法的情况下的默认值，该参数有默认值DEFAULT_DOUBLE_VALUE，分两种情况
																当值是   DEFAULT_DOUBLE_VALUE 时，返回0（值不可信）
																当值不是 DEFAULT_DOUBLE_VALUE 时，令value=def_value并返回1
		   const bool group_is_case_sensitive = false     : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false      : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：
  说    明：
***************************************************************************/
int config_file_tools::item_get_double(const char *const group_name, const char *const item_name, double &value,
									   const double min_value, const double max_value, const double def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	if (group_name == nullptr || item_name == nullptr)
	{
		if (def_value != DEFAULT_DOUBLE_VALUE)
		{
			value = def_value;
			return 1;
		}
		return 0;
	}
	string item_value;
	if (!this->get_value(item_value, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive))
	{
		// 未取到值
		if (def_value != DEFAULT_DOUBLE_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	double v;
	if (!try_parse_number(item_value, v))
	{
		if (def_value != DEFAULT_DOUBLE_VALUE)
		{
			value = def_value;
			return 1;
		}
		return 0;
	}
	// 检查合法性
	if (v < min_value || v > max_value)
	{
		// 非法值
		if (def_value != DEFAULT_DOUBLE_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	// 合法值
	value = v;
	return 1;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：因为工具函数一般在程序初始化阶段被调用，不会在程序执行中被高频次调用，
		   因此这里直接套壳，会略微影响效率，但不影响整体性能（对高频次调用，此方法不适合）
***************************************************************************/
int config_file_tools::item_get_double(const string &group_name, const string &item_name, double &value,
									   const double min_value, const double max_value, const double def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 本函数已实现 */
	return this->item_get_double(group_name.c_str(), item_name.c_str(), value, min_value, max_value, def_value, group_is_case_sensitive, item_is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：取某项的内容，返回类型为char * / char []型
  输入参数：const char* const group_name                  ：组名
		   const char* const item_name                   ：项名
		   char *const value                             ：读到的C方式的字符串（返回1时可信，返回0则不可信）
		   const int str_maxlen                          ：指定要读的最大长度（含尾零）
																如果<1则返回空串(不是DEFAULT_CSTRING_VALUE，虽然现在两者相同，但要考虑default值可能会改)
																如果>MAX_STRLEN 则上限为MAX_STRLEN
		   const char* const def_str                     ：读不到情况下的默认值，该参数有默认值DEFAULT_CSTRING_VALUE，分两种情况
																当值是   DEFAULT_CSTRING_VALUE 时，返回0（值不可信）
																当值不是 DEFAULT_CSTRING_VALUE 时，令value=def_value并返回1（注意，不是直接=）
		   const bool group_is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false  : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：
  说    明：1、为简化，未对\"等做转义处理，均按普通字符
		   2、含尾零的最大长度为str_maxlen，调用时要保证有足够空间
		   3、如果 str_maxlen 超过了系统预设的上限 MAX_STRLEN，则按 MAX_STRLEN 取
***************************************************************************/
int config_file_tools::item_get_cstring(const char *const group_name, const char *const item_name, char *const value,
										const int str_maxlen, const char *const def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	if (value == nullptr)
		return 0;
	if (str_maxlen < 1)
	{
		// 无可写空间，仅返回失败
		return 0;
	}
	if (group_name == nullptr || item_name == nullptr)
	{
		if (def_value != DEFAULT_CSTRING_VALUE && def_value != nullptr)
		{
			int actual_maxlen = (str_maxlen > MAX_STRLEN) ? MAX_STRLEN : str_maxlen;
			strncpy(value, def_value, static_cast<size_t>(actual_maxlen) - 1);
			value[actual_maxlen - 1] = '\0';
			return 1;
		}
		value[0] = '\0';
		return 1;
	}
	int actual_maxlen = (str_maxlen > MAX_STRLEN) ? MAX_STRLEN : str_maxlen;
	string item_value;
	// 获取值
	if (!this->get_value(item_value, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive))
	{
		// 未取到值
		if (def_value != DEFAULT_CSTRING_VALUE && def_value != nullptr)
		{
			// 设为默认值
			strncpy(value, def_value, static_cast<size_t>(actual_maxlen) - 1);
			value[actual_maxlen - 1] = '\0'; // 确保尾零
			return 1;
		}
		else
		{
			// 返回空串
			value[0] = '\0';
			return 0;
		}
	}
	// 取到值，进行流输入
	strncpy(value, item_value.c_str(), static_cast<size_t>(actual_maxlen) - 1);
	value[actual_maxlen - 1] = '\0';
	return 1;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：因为工具函数一般在程序初始化阶段被调用，不会在程序执行中被高频次调用，
		   因此这里直接套壳，会略微影响效率，但不影响整体性能（对高频次调用，此方法不适合）
***************************************************************************/
int config_file_tools::item_get_cstring(const string &group_name, const string &item_name, char *const value,
										const int str_maxlen, const char *const def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)

{
	/* 本函数已实现 */
	return item_get_cstring(group_name.c_str(), item_name.c_str(), value, str_maxlen, def_value, group_is_case_sensitive, item_is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：取某项的内容，返回类型为 string 型
  输入参数：const char* const group_name               ：组名
		   const char* const item_name                ：项名
		   string &value                              ：读到的string方式的字符串（返回1时可信，返回0则不可信）
		   const string &def_value                    ：读不到情况下的默认值，该参数有默认值DEFAULT_STRING_VALUE，分两种情况
															当值是   DEFAULT_STRING_VALUE 时，返回0（值不可信）
															当值不是 DEFAULT_STRING_VALUE 时，令value=def_value并返回1
		   const bool group_is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false  : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：
  说    明：为简化，未对\"等做转义处理，均按普通字符
***************************************************************************/
int config_file_tools::item_get_string(const char *const group_name, const char *const item_name, string &value,
									   const string &def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	if (group_name == nullptr || item_name == nullptr)
	{
		if (def_value != DEFAULT_STRING_VALUE)
		{
			value = def_value;
			return 1;
		}
		return 0;
	}
	string item_value;
	if (!this->get_value(item_value, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive) || item_value.empty())
	{
		// 未取到值
		if (def_value != DEFAULT_STRING_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	// 取到值（保持原始空格内容）
	value = item_value;
	return 1;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：因为工具函数一般在程序初始化阶段被调用，不会在程序执行中被高频次调用，
		   因此这里直接套壳，会略微影响效率，但不影响整体性能（对高频次调用，此方法不适合）
***************************************************************************/
int config_file_tools::item_get_string(const string &group_name, const string &item_name, string &value,
									   const string &def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 本函数已实现 */
	return this->item_get_string(group_name.c_str(), item_name.c_str(), value, def_value, group_is_case_sensitive, item_is_case_sensitive);
}

/***************************************************************************
  函数名称：
  功    能：取某项的内容，返回类型为 IPv4 地址的32bit整型（主机序）
  输入参数：const char* const group_name               ：组名
		   const char* const item_name                ：项名
		   unsigned int &value                        ：读到的IP地址，32位整型方式（返回1时可信，返回0则不可信）
		   const unsigned int &def_value              ：读不到情况下的默认值，该参数有默认值DEFAULT_IPADDR_VALUE，分两种情况
															当值是   DEFAULT_IPADDR_VALUE 时，返回0（值不可信）
															当值不是 DEFAULT_IPADDR_VALUE 时，令value=def_value并返回1
		   const bool group_is_case_sensitive = false : 组名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
		   const bool item_is_case_sensitive = false  : 项名是否大小写敏感，true-大小写敏感 / 默认false-大小写不敏感
  返 回 值：
  说    明：
***************************************************************************/
int config_file_tools::item_get_ipaddr(const char *const group_name, const char *const item_name, unsigned int &value,
									   const unsigned int &def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 按需完成，根据需要改变return的值 */
	if (group_name == nullptr || item_name == nullptr)
	{
		if (def_value != DEFAULT_IPADDR_VALUE)
		{
			value = def_value;
			return 1;
		}
		return 0;
	}
	string item_value;
	if (!this->get_value(item_value, string(group_name), string(item_name), group_is_case_sensitive, item_is_case_sensitive))
	{
		// 未取到值
		if (def_value != DEFAULT_IPADDR_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	// 取到值，流输入
	stringstream ss;
	ss << item_value;
	string ip_str;
	ss >> ip_str;
	if (!is_valid_ip_str(ip_str))
	{
		// 非法值
		if (def_value != DEFAULT_IPADDR_VALUE)
		{
			value = def_value;
			return 1;
		}
		else
			return 0;
	}
	// 合法值
	value = ip_str_to_value(ip_str);
	return 1;
}

/***************************************************************************
  函数名称：
  功    能：组名/项目为string方式，其余同上
  输入参数：
  返 回 值：
  说    明：因为工具函数一般在程序初始化阶段被调用，不会在程序执行中被高频次调用，
		   因此这里直接套壳，会略微影响效率，但不影响整体性能（对高频次调用，此方法不适合）
***************************************************************************/
int config_file_tools::item_get_ipaddr(const string &group_name, const string &item_name, unsigned int &value,
									   const unsigned int &def_value, const bool group_is_case_sensitive, const bool item_is_case_sensitive)
{
	/* 本函数已实现 */
	return this->item_get_ipaddr(group_name.c_str(), item_name.c_str(), value, def_value, group_is_case_sensitive, item_is_case_sensitive);
}
