#include "VansShaderCompiler.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace Vans
{
	namespace
	{
		constexpr DWORD kCompileTimeoutMs = 60000;
		std::atomic<std::uint64_t> g_TemporaryDirectorySequence{ 1 };

		std::filesystem::path NormalizePath(const std::filesystem::path& path)
		{
			std::error_code ec;
			std::filesystem::path result = std::filesystem::weakly_canonical(path, ec);
			if (ec)
			{
				ec.clear();
				result = std::filesystem::absolute(path, ec);
			}
			return result.lexically_normal();
		}

		std::wstring PathKey(const std::filesystem::path& path)
		{
			std::wstring key = NormalizePath(path).wstring();
#if defined(_WIN32)
			std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
			return key;
		}

		std::string LowerExtension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			if (!extension.empty() && extension.front() == '.')
				extension.erase(extension.begin());
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return extension;
		}

		bool IsSupportedStage(const std::string& stage)
		{
			static const std::unordered_set<std::string> stages =
			{
				"vert", "tesc", "tese", "geom", "frag", "comp",
				"rgen", "rmiss", "rahit", "rchit", "rint"
			};
			return stages.find(stage) != stages.end();
		}

		std::string SanitizeName(std::string value)
		{
			for (char& c : value)
			{
				const unsigned char byte = static_cast<unsigned char>(c);
				if (!std::isalnum(byte) && c != '_' && c != '-')
					c = '_';
			}
			return value.empty() ? "Shader" : value;
		}

		std::wstring QuoteArgument(const std::filesystem::path& path)
		{
			return L"\"" + path.wstring() + L"\"";
		}

		std::string ReadTextFile(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return {};
			std::ostringstream stream;
			stream << input.rdbuf();
			return stream.str();
		}

		bool RunCompilerProcess(
			const std::wstring& commandLine,
			const std::filesystem::path& logPath,
			DWORD& exitCode,
			std::string& diagnostic)
		{
			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;

			HANDLE logHandle = CreateFileW(
				logPath.c_str(),
				GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				&security,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
			if (logHandle == INVALID_HANDLE_VALUE)
			{
				diagnostic = "Failed to create shader compiler diagnostic file";
				return false;
			}

			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			startup.dwFlags = STARTF_USESTDHANDLES;
			startup.hStdOutput = logHandle;
			startup.hStdError = logHandle;
			startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

			PROCESS_INFORMATION process{};
			std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
			mutableCommand.push_back(L'\0');

			const BOOL created = CreateProcessW(
				nullptr,
				mutableCommand.data(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&startup,
				&process);
			if (!created)
			{
				CloseHandle(logHandle);
				diagnostic = "Failed to start glslangValidator (Win32 error " + std::to_string(GetLastError()) + ")";
				return false;
			}

			const DWORD waitResult = WaitForSingleObject(process.hProcess, kCompileTimeoutMs);
			if (waitResult == WAIT_TIMEOUT)
			{
				TerminateProcess(process.hProcess, ERROR_TIMEOUT);
				WaitForSingleObject(process.hProcess, INFINITE);
				diagnostic = "glslangValidator timed out";
			}

			exitCode = ERROR_GEN_FAILURE;
			GetExitCodeProcess(process.hProcess, &exitCode);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			CloseHandle(logHandle);

			const std::string processOutput = ReadTextFile(logPath);
			if (!processOutput.empty())
				diagnostic = processOutput;
			return waitResult == WAIT_OBJECT_0 && exitCode == 0;
		}

		bool ReadSpirv(const std::filesystem::path& path, std::vector<std::uint32_t>& words, std::string& error)
		{
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			if (!input)
			{
				error = "Cannot read compiler output: " + path.string();
				return false;
			}

			const std::streamsize size = input.tellg();
			if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0)
			{
				error = "Invalid SPIR-V byte size: " + path.string();
				return false;
			}

			input.seekg(0, std::ios::beg);
			words.resize(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
			if (!input.read(reinterpret_cast<char*>(words.data()), size))
			{
				error = "Failed to read SPIR-V payload: " + path.string();
				return false;
			}

			if (words.empty() || words.front() != 0x07230203u)
			{
				error = "Compiler output is not valid SPIR-V: " + path.string();
				return false;
			}
			return true;
		}

		std::filesystem::path ResolveInclude(
			const std::filesystem::path& includingFile,
			const std::string& includeText,
			const VansShaderCompileRequest& request,
			bool& found)
		{
			std::vector<std::filesystem::path> candidates;
			candidates.push_back(includingFile.parent_path() / includeText);
			candidates.push_back(request.sourceFolder / includeText);
			for (const auto& root : request.includeRoots)
				candidates.push_back(root / includeText);

			std::error_code ec;
			for (const auto& candidate : candidates)
			{
				if (std::filesystem::is_regular_file(candidate, ec) && !ec)
				{
					found = true;
					return NormalizePath(candidate);
				}
				ec.clear();
			}

			found = false;
			return NormalizePath(candidates.front());
		}
	}

	bool VansShaderCompiler::IsShaderSourceExtension(const std::filesystem::path& path)
	{
		const std::string extension = LowerExtension(path);
		return IsSupportedStage(extension) || extension == "glsl";
	}

	VansShaderDependencyScanResult VansShaderCompiler::ScanDependencies(const VansShaderCompileRequest& request) const
	{
		VansShaderDependencyScanResult result;
		std::unordered_set<std::wstring> visited;
		std::unordered_set<std::wstring> resolved;
		std::unordered_set<std::wstring> unresolved;
		const std::regex includePattern(R"(^\s*#\s*include\s*[\"<]([^\">]+)[\">])");

		std::function<void(const std::filesystem::path&)> scanFile;
		scanFile = [&](const std::filesystem::path& inputPath)
		{
			const std::filesystem::path path = NormalizePath(inputPath);
			const std::wstring key = PathKey(path);
			if (!visited.emplace(key).second)
				return;

			std::ifstream input(path);
			if (!input)
			{
				if (unresolved.emplace(key).second)
					result.unresolvedDependencies.push_back(path);
				return;
			}

			std::string line;
			while (std::getline(input, line))
			{
				std::smatch match;
				if (!std::regex_search(line, match, includePattern) || match.size() < 2)
					continue;

				bool found = false;
				const std::filesystem::path dependency = ResolveInclude(path, match[1].str(), request, found);
				const std::wstring dependencyKey = PathKey(dependency);
				if (!found)
				{
					if (unresolved.emplace(dependencyKey).second)
						result.unresolvedDependencies.push_back(dependency);
					continue;
				}

				if (resolved.emplace(dependencyKey).second)
					result.resolvedDependencies.push_back(dependency);
				scanFile(dependency);
			}
		};

		for (const auto& stage : request.stages)
			scanFile(stage.sourcePath);
		return result;
	}

	VansShaderCompileResult VansShaderCompiler::Compile(const VansShaderCompileRequest& request) const
	{
		VansShaderCompileResult result;
		result.programId = request.programId;
		result.sourceRevision = request.sourceRevision;
		result.dependencies = ScanDependencies(request);

		if (request.programId.empty() || request.stages.empty())
		{
			result.diagnostics.emplace_back("Shader compile request has no program id or stages");
			return result;
		}

		std::filesystem::path temporaryRoot = request.artifactRoot;
		if (temporaryRoot.empty())
			temporaryRoot = std::filesystem::temp_directory_path() / "ForestEngine" / "ShaderHotReload";
		temporaryRoot /= ".tmp";
		temporaryRoot /= SanitizeName(request.programId) + "_" +
			std::to_string(request.sourceRevision) + "_" +
			std::to_string(g_TemporaryDirectorySequence.fetch_add(1, std::memory_order_relaxed));

		std::error_code ec;
		std::filesystem::create_directories(temporaryRoot, ec);
		if (ec)
		{
			result.diagnostics.emplace_back("Cannot create shader temporary directory: " + temporaryRoot.string());
			return result;
		}

		for (std::size_t index = 0; index < request.stages.size(); ++index)
		{
			const auto& stage = request.stages[index];
			const std::string stageName = stage.stage.empty() ? LowerExtension(stage.sourcePath) : stage.stage;
			if (!IsSupportedStage(stageName))
			{
				result.diagnostics.emplace_back("Unsupported shader stage '" + stageName + "': " + stage.sourcePath.string());
				break;
			}

			const std::filesystem::path outputPath = temporaryRoot / (std::to_string(index) + ".spv");
			const std::filesystem::path logPath = temporaryRoot / (std::to_string(index) + ".log");
			std::wstring command = L"glslangValidator -V " + QuoteArgument(stage.sourcePath) +
				L" -o " + QuoteArgument(outputPath) + L" --target-env vulkan1.2";
			for (const auto& includeRoot : request.includeRoots)
				command += L" -I" + QuoteArgument(includeRoot);

			DWORD exitCode = ERROR_GEN_FAILURE;
			std::string processDiagnostic;
			if (!RunCompilerProcess(command, logPath, exitCode, processDiagnostic))
			{
				std::ostringstream message;
				message << "Shader compile failed for " << stage.sourcePath.string()
					<< " (exit=" << exitCode << ")";
				if (!processDiagnostic.empty())
					message << "\n" << processDiagnostic;
				result.diagnostics.push_back(message.str());
				break;
			}

			VansCompiledShaderStage compiled;
			compiled.stage = stageName;
			compiled.entryPoint = stage.entryPoint;
			std::string readError;
			if (!ReadSpirv(outputPath, compiled.spirv, readError))
			{
				result.diagnostics.push_back(std::move(readError));
				break;
			}
			result.stages.emplace_back(std::move(compiled));
		}

		result.success = result.stages.size() == request.stages.size();
		std::filesystem::remove_all(temporaryRoot, ec);
		return result;
	}
}
