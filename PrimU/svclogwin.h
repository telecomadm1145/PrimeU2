#pragma once
// svc_names.h 以及相关的 ImGui 渲染逻辑封装

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <algorithm>

// 假设这些头文件在你的项目中可用
#include "imgui.h"
#include "logbuf.h"
#include "log.h"

// ============================================================================
// SVC 名称注册表 (保持单例模式)
// ============================================================================

class SVCNameRegistry {

public:

	static SVCNameRegistry& Instance() {

		static SVCNameRegistry inst;

		return inst;

	}


	/// 从 JSON 文件加载，格式：{ "0xHEX": "Name", ... }

	/// 返回加载的条目数，失败返回 -1

	int LoadFromFile(const std::string& path) {

		std::ifstream ifs(path);

		if (!ifs.is_open()) {

			fprintf(stderr, "[SVCNameRegistry] Failed to open: %s\n", path.c_str());

			return -1;

		}


		std::stringstream ss;

		ss << ifs.rdbuf();

		std::string content = ss.str();


		return Parse(content);

	}


	/// 从内存字符串加载

	int LoadFromString(const std::string& json) {

		return Parse(json);

	}


	/// 查找 SVC 名称，未找到返回 nullptr

	const char* Lookup(unsigned int svc) const {

		auto it = names_.find(svc);

		if (it != names_.end())

			return it->second.c_str();

		return nullptr;

	}


	/// 获取格式化名称：有则 "Name (0xHEX)"，无则 "0xHEX"

	std::string Format(unsigned int svc) const {

		const char* name = Lookup(svc);

		char buf[256];

		if (name)

			snprintf(buf, sizeof(buf), "%s", name);

		else

			snprintf(buf, sizeof(buf), "0x%X", svc);

		return buf;

	}


	/// 条目总数

	size_t Count() const { return names_.size(); }


	/// 清空

	void Clear() { names_.clear(); }


	/// 遍历所有条目

	const std::unordered_map<unsigned int, std::string>& GetAll() const {

		return names_;

	}


private:

	SVCNameRegistry() = default;


	// ==================================================================

	//  极简 JSON 解析：只处理 { "key": "value", ... } 格式

	//  - 忽略空白、换行

	//  - key 必须是 "0x..." 或纯十进制数字字符串

	//  - value 是普通字符串

	//  - 支持转义 \" \\ \/ \n \t 等

	// ==================================================================

	int Parse(const std::string& json) {

		const char* p = json.c_str();

		const char* end = p + json.size();

		int count = 0;


		// 跳过空白

		auto skipWS = [&]() {

			while (p < end && (*p == ' ' || *p == '\t' ||

				*p == '\n' || *p == '\r'))

				++p;

			};


		skipWS();


		// 期望 '{'

		if (p >= end || *p != '{') {

			fprintf(stderr, "[SVCNameRegistry] Expected '{' at start\n");

			return -1;

		}

		++p; // skip '{'


		while (p < end) {

			skipWS();


			// 结束 '}'

			if (p < end && *p == '}')

				break;


			// 逗号分隔

			if (p < end && *p == ',') {

				++p;

				skipWS();

			}


			// 检查结束

			if (p < end && *p == '}')

				break;


			// 解析 key

			std::string key;

			if (!parseString(p, end, key)) {

				fprintf(stderr, "[SVCNameRegistry] Failed to parse key at offset %zu\n",

					(size_t)(p - json.c_str()));

				return -1;

			}


			skipWS();


			// 期望 ':'

			if (p >= end || *p != ':') {

				fprintf(stderr, "[SVCNameRegistry] Expected ':' at offset %zu\n",

					(size_t)(p - json.c_str()));

				return -1;

			}

			++p;

			skipWS();


			// 解析 value

			std::string value;

			if (!parseString(p, end, value)) {

				fprintf(stderr, "[SVCNameRegistry] Failed to parse value at offset %zu\n",

					(size_t)(p - json.c_str()));

				return -1;

			}


			// key → unsigned int

			unsigned int svcNum = 0;

			if (!parseHexOrDec(key, svcNum)) {

				fprintf(stderr, "[SVCNameRegistry] Invalid number key: '%s'\n",

					key.c_str());

				return -1;

			}


			names_[svcNum] = std::move(value);

			++count;

		}


		printf("[SVCNameRegistry] Loaded %d SVC names\n", count);

		return count;

	}


	// 解析 JSON 字符串 "..."，支持基本转义

	static bool parseString(const char*& p, const char* end, std::string& out) {

		out.clear();

		if (p >= end || *p != '"')

			return false;

		++p; // skip opening '"'


		while (p < end) {

			char c = *p++;

			if (c == '"') {

				return true; // 正常结束

			}

			if (c == '\\' && p < end) {

				char esc = *p++;

				switch (esc) {

				case '"':  out += '"';  break;

				case '\\': out += '\\'; break;

				case '/':  out += '/';  break;

				case 'n':  out += '\n'; break;

				case 't':  out += '\t'; break;

				case 'r':  out += '\r'; break;

				case 'b':  out += '\b'; break;

				case 'f':  out += '\f'; break;

				case 'u': {

					// \uXXXX — 简单处理，跳过4位

					if (p + 4 <= end) {

						char hex[5] = {};

						memcpy(hex, p, 4);

						p += 4;

						unsigned int cp = (unsigned int)strtoul(hex, nullptr, 16);

						if (cp < 0x80) {

							out += (char)cp;

						}

						else if (cp < 0x800) {

							out += (char)(0xC0 | (cp >> 6));

							out += (char)(0x80 | (cp & 0x3F));

						}

						else {

							out += (char)(0xE0 | (cp >> 12));

							out += (char)(0x80 | ((cp >> 6) & 0x3F));

							out += (char)(0x80 | (cp & 0x3F));

						}

					}

					break;

				}

				default: out += esc; break;

				}

			}

			else {

				out += c;

			}

		}

		return false; // 未闭合

	}


	// "0x1000a" → 0x1000a, "65536" → 65536

	static bool parseHexOrDec(const std::string& s, unsigned int& out) {

		if (s.empty()) return false;

		char* endptr = nullptr;

		unsigned long val;


		if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))

			val = strtoul(s.c_str(), &endptr, 16);

		else

			val = strtoul(s.c_str(), &endptr, 10);


		if (endptr == s.c_str() || *endptr != '\0')

			return false;

		out = static_cast<unsigned int>(val);

		return true;

	}


	std::unordered_map<unsigned int, std::string> names_;

};




// ============================================================================
// 辅助渲染函数
// ============================================================================
inline ImVec4 GetSVCColor(unsigned int svc) {
	// 简单的颜色分组逻辑
	if ((svc & 0xFFF00) == 0x10000) return ImVec4(0.4f, 0.7f, 1.0f, 1.0f); // 核心
	if ((svc & 0xFFF00) == 0x10100) return ImVec4(0.8f, 0.5f, 0.9f, 1.0f); // IPC
	return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

inline void RenderSVCRow(int displayIdx, const SVCCallRecord& rec,
	std::chrono::steady_clock::time_point baseTime,
	const SVCNameRegistry& reg, bool showTimestamp,
	bool showCallerPC, bool showArgs)
{
	const char* svcName = reg.Lookup(rec.svc);
	ImVec4 color = GetSVCColor(rec.svc);

	ImGui::TableNextRow();

	// # 编号
	ImGui::TableNextColumn();
	ImGui::TextDisabled("%d", displayIdx);

	// SVC 编号
	ImGui::TableNextColumn();
	ImGui::TextColored(color, "0x%X", rec.svc);

	// 名称
	ImGui::TableNextColumn();
	if (svcName) {
		ImGui::TextColored(color, "%s", svcName);
	}
	else {
		ImGui::TextDisabled("(unknown)");
	}

	// 悬停提示 (深度信息展示)
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::TextColored(color, "SVC 0x%X Detail", rec.svc);
		ImGui::Separator();
		ImGui::Text("Thread ID: %u", rec.threadid);
		ImGui::Text("PC: 0x%08X", rec.ssa.caller_pc);
		ImGui::Text("SP: 0x%08X", rec.ssa.sp);
		ImGui::Separator();
		ImGui::Columns(2, "regs", false);
		ImGui::Text("R0: 0x%08X", rec.ssa.r0); ImGui::NextColumn();
		ImGui::Text("R1: 0x%08X", rec.ssa.r1); ImGui::NextColumn();
		ImGui::Text("R2: 0x%08X", rec.ssa.r2); ImGui::NextColumn();
		ImGui::Text("R3: 0x%08X", rec.ssa.r3);
		ImGui::Columns(1);
		ImGui::EndTooltip();
	}

	ImGui::TableNextColumn();
	ImGui::Text("%u", rec.threadid);

	if (showTimestamp) {
		ImGui::TableNextColumn();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(rec.timestamp - baseTime);
		ImGui::Text("%.3f ms", elapsed.count() / 1000.0);
	}

	if (showCallerPC) {
		ImGui::TableNextColumn();
		ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%08X", rec.ssa.caller_pc);
	}

	if (showArgs) {
		ImGui::TableNextColumn(); ImGui::Text("%08X", rec.ssa.r0);
		ImGui::TableNextColumn(); ImGui::Text("%08X", rec.ssa.r1);
		ImGui::TableNextColumn(); ImGui::Text("%08X", rec.ssa.r2);
		ImGui::TableNextColumn(); ImGui::Text("%08X", rec.ssa.r3);
		ImGui::TableNextColumn(); ImGui::Text("%08X", rec.ssa.sp);
	}
}

// ============================================================================
// 主窗口逻辑
// ============================================================================
template<typename T>
class RollingLogBuffer; // 假设这是你的缓冲类

inline void DrawSVCLogWindow(RollingLogBuffer<SVCCallRecord>& logBuffer, bool* p_open = nullptr) {
	// 静态持久化状态
	static bool autoScroll = true;
	static bool showTimestamp = true;
	static bool showArgs = true;
	static bool showCallerPC = true;
	static bool pauseUpdate = false;
	static char filterText[128] = "";

	// 【暂停功能的核心】：静态容器存储快照
	static std::vector<SVCCallRecord> frozenSnapshot;
	static bool wasPaused = false;

	ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("SVC Call Log", p_open, ImGuiWindowFlags_MenuBar)) {
		ImGui::End();
		return;
	}

	// 处理键盘快捷键 (空格键暂停)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space)) {
		pauseUpdate = !pauseUpdate;
	}

	auto& reg = SVCNameRegistry::Instance();

	// ----------------------------------------------------------------
	// 1. 菜单栏
	// ----------------------------------------------------------------
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Clear Log")) {
				logBuffer.clear();
				frozenSnapshot.clear();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Pause Updates", "Space", &pauseUpdate);
			ImGui::MenuItem("Auto-scroll", nullptr, &autoScroll);
			ImGui::Separator();
			ImGui::MenuItem("Show Timestamp", nullptr, &showTimestamp);
			ImGui::MenuItem("Show Arguments", nullptr, &showArgs);
			ImGui::MenuItem("Show Caller PC", nullptr, &showCallerPC);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// ----------------------------------------------------------------
	// 2. 工具栏与状态
	// ----------------------------------------------------------------
	{
		// 暂停按钮 (醒目的颜色)
		if (pauseUpdate) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
			if (ImGui::Button("RESUME")) pauseUpdate = false;
		}
		else {
			if (ImGui::Button("PAUSE")) pauseUpdate = true;
		}
		if (pauseUpdate) ImGui::PopStyleColor(2);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(200);
		ImGui::InputTextWithHint("##filter", "Filter (Name/Hex/Dec)", filterText, sizeof(filterText));

		ImGui::SameLine();
		if (ImGui::Button("Clear")) filterText[0] = '\0';

		ImGui::SameLine();
		//ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		ImGui::TextDisabled("Entries: %zu | Names: %zu", logBuffer.size(), reg.Count());

		if (pauseUpdate) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[FROZEN VIEW]");
		}
	}

	ImGui::Separator();

	// ----------------------------------------------------------------
	// 3. 暂停逻辑处理：抓取快照
	// ----------------------------------------------------------------
	// 当状态从运行转为暂停，或者在暂停状态下 buffer 被外部改变时
	if (pauseUpdate) {
		if (!wasPaused) {
			// 刚按下暂停的一瞬间，把当前的 snapshot 存下来
			frozenSnapshot = logBuffer.snapshot();
			wasPaused = true;
		}
	}
	else {
		wasPaused = false;
		if (!frozenSnapshot.empty()) frozenSnapshot.clear();
	}

	// ----------------------------------------------------------------
	// 4. 数据展示逻辑
	// ----------------------------------------------------------------
	int colCount = 4 + (showTimestamp ? 1 : 0) + (showCallerPC ? 1 : 0) + (showArgs ? 5 : 0);
	ImGuiTableFlags tblFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
		ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
		ImGuiTableFlags_Hideable;

	if (ImGui::BeginTable("##svc_table", colCount, tblFlags, ImVec2(0, 0))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
		ImGui::TableSetupColumn("SVC", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		if (showTimestamp) ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
		if (showCallerPC)  ImGui::TableSetupColumn("PC", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		if (showArgs) {
			ImGui::TableSetupColumn("R0", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("R1", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("R2", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("R3", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("SP", ImGuiTableColumnFlags_WidthFixed, 75.0f);
		}
		ImGui::TableHeadersRow();

		// 决定使用实时视图还是快照
		const auto& dataToRender = pauseUpdate ? frozenSnapshot : logBuffer.snapshot();

		if (!dataToRender.empty()) {
			static std::chrono::steady_clock::time_point baseTime = dataToRender[0].timestamp;

			// 过滤匹配逻辑
			bool hasFilter = (filterText[0] != '\0');
			std::string fStr = filterText;
			std::transform(fStr.begin(), fStr.end(), fStr.begin(), ::tolower);

			ImGuiListClipper clipper;
			// 如果有过滤，不能直接用 clipper (因为索引不连续)，这里为了深度展示做简单循环
			// 如果追求极致性能，建议预先在暂停/更新时生成一个 filteredIndex 列表
			int displayIdx = 0;
			for (size_t i = 0; i < dataToRender.size(); ++i) {
				const auto& rec = dataToRender[i];

				if (hasFilter) {
					const char* name = reg.Lookup(rec.svc);
					std::string nStr = name ? name : "";
					std::transform(nStr.begin(), nStr.end(), nStr.begin(), ::tolower);
					char hexBuf[16]; snprintf(hexBuf, sizeof(hexBuf), "0x%x", rec.svc);

					if (nStr.find(fStr) == std::string::npos &&
						std::string(hexBuf).find(fStr) == std::string::npos)
						continue;
				}

				RenderSVCRow(displayIdx++, rec, baseTime, reg, showTimestamp, showCallerPC, showArgs);
			}
		}

		// 自动滚动 (仅在未暂停且开启自动滚动时生效)
		if (!pauseUpdate && autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
			ImGui::SetScrollHereY(1.0f);
		}

		ImGui::EndTable();
	}
	ImGui::End();
}