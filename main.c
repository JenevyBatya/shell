#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CMD_LENGTH 256
#define MAX_PROCESSES 150
#define MAX_WAIT_OBJECTS 64

typedef struct {
    PROCESS_INFORMATION pi;
    clock_t start_time;
    char program_name[MAX_CMD_LENGTH];
    HANDLE stdout_pipe_read;
    HANDLE stdout_pipe_write;
    HANDLE stderr_pipe_read;
    HANDLE stderr_pipe_write;
} ProcessInfo;

void read_and_log_output(HANDLE pipe, const char *label) {
    DWORD bytes_read;
    char buffer[2048];

    while (1) {
        if (!ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, NULL) || bytes_read == 0) {
            break;
        }
        buffer[bytes_read] = '\0';
        printf("[%s]: %s", label, buffer);
    }
}

//следит за завершением процессов и логирует их результаты
void log_processes(void *param) {
    ProcessInfo *processes = (ProcessInfo *) param;
    int process_count = 0;

    for (int i = 0; processes[i].pi.hProcess != NULL; i++) {
        process_count++;
    }

    while (process_count > 0) {
        int group_size = (process_count > MAX_WAIT_OBJECTS) ? MAX_WAIT_OBJECTS : process_count;
        HANDLE handles[MAX_WAIT_OBJECTS];

        for (int i = 0; i < group_size; i++) {
            handles[i] = processes[i].pi.hProcess;
        }

        DWORD finished = WaitForMultipleObjects(group_size, handles, FALSE, INFINITE);
        int index = finished - WAIT_OBJECT_0;

        if (index >= 0 && index < group_size) {
            read_and_log_output(processes[index].stdout_pipe_read, processes[index].program_name);
            read_and_log_output(processes[index].stderr_pipe_read, processes[index].program_name);

            clock_t end_time = clock();
            double time_taken = (double) (end_time - processes[index].start_time) / CLOCKS_PER_SEC;

            printf("Program '%s' finished. Time taken: %.2f seconds.\n",
                   processes[index].program_name, time_taken);

            CloseHandle(processes[index].pi.hProcess);
            CloseHandle(processes[index].pi.hThread);
            CloseHandle(processes[index].stdout_pipe_read);
            CloseHandle(processes[index].stderr_pipe_read);

            for (int j = index; j < process_count - 1; j++) {
                processes[j] = processes[j + 1];
            }
            process_count--;
        }
    }

    free(processes);
}

void run_program(const char *program, int times, ProcessInfo *processes, int *process_count) {
    HMODULE hModule = GetModuleHandle("kernel32.dll");
    if (!hModule) {
        printf("Could not get handle to kernel32.dll\n");
        return;
    }

    typedef BOOL (WINAPI *CreateProcessInternal_t)(
        HANDLE, LPWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
        DWORD, LPVOID, LPWSTR, LPSTARTUPINFO, LPPROCESS_INFORMATION);

    CreateProcessInternal_t pCreateProcessInternal = (CreateProcessInternal_t) GetProcAddress(
        hModule, "CreateProcessInternalW");
    if (!pCreateProcessInternal) {
        printf("Could not get CreateProcessInternal address\n");
        return;
    }

    for (int i = 0; i < times; i++) {
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
        HANDLE stdout_pipe_read, stdout_pipe_write;
        HANDLE stderr_pipe_read, stderr_pipe_write;

        if (!CreatePipe(&stdout_pipe_read, &stdout_pipe_write, &sa, 0) ||
            !CreatePipe(&stderr_pipe_read, &stderr_pipe_write, &sa, 0)) {
            printf("Failed to create pipes for program '%s'.\n", program);
            return;
        }

        SetHandleInformation(stdout_pipe_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stderr_pipe_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdOutput = stdout_pipe_write;
        si.hStdError = stderr_pipe_write;
        si.dwFlags |= STARTF_USESTDHANDLES;

        ZeroMemory(&pi, sizeof(pi));

        wchar_t commandLine[MAX_CMD_LENGTH];
        mbstowcs(commandLine, program, MAX_CMD_LENGTH);

        LARGE_INTEGER frequency, start_time, end_time;
        QueryPerformanceFrequency(&frequency);

        QueryPerformanceCounter(&start_time);
        if (!pCreateProcessInternal(
            NULL, // hToken
            NULL,// lpApplicationName
            commandLine,// lpCommandLine
            NULL,// lpProcessAttributes
            NULL, // lpThreadAttributes
            TRUE,// bInheritHandles
            0,// dwCreationFlags
            NULL, // lpEnvironment
            NULL,// lpCurrentDirectory
            &si,// lpStartupInfo
            &pi// lpProcessInformation
            )) {
            printf("Failed to start program '%s' (%d).\n", program, GetLastError());
            CloseHandle(stdout_pipe_read);
            CloseHandle(stdout_pipe_write);
            CloseHandle(stderr_pipe_read);
            CloseHandle(stderr_pipe_write);
            continue;
        }
        QueryPerformanceCounter(&end_time);

        CloseHandle(stdout_pipe_write);
        CloseHandle(stderr_pipe_write);

        double elapsed_time = (double) (end_time.QuadPart - start_time.QuadPart) * 1000.0 / frequency.QuadPart;

        printf("Started program '%s' (instance %d) in %.3f ms.\n", program, i + 1, elapsed_time);

        processes[*process_count].pi = pi;
        processes[*process_count].start_time = clock();
        strncpy(processes[*process_count].program_name, program, MAX_CMD_LENGTH);
        processes[*process_count].stdout_pipe_read = stdout_pipe_read;
        processes[*process_count].stderr_pipe_read = stderr_pipe_read;
        (*process_count)++;
    }
}

void parse_and_run(const char *command) {
    if (strncmp(command, "run ", 4) != 0) {
        printf("Invalid command. Use 'run <program1>:<times1>,<program2>:<times2>,...'\n");
        return;
    }

    const char *programs_list = command + 4;
    char programs_copy[MAX_CMD_LENGTH];
    strncpy(programs_copy, programs_list, MAX_CMD_LENGTH);
    programs_copy[MAX_CMD_LENGTH - 1] = '\0';

    char *token = strtok(programs_copy, ",");
    ProcessInfo *processes = malloc(MAX_PROCESSES * sizeof(ProcessInfo));
    int process_count = 0;

    while (token != NULL) {
        char *colon = strchr(token, ' ');
        if (colon == NULL) {
            printf("Invalid format for program '%s'. Use '<program>:<times>'\n", token);
            token = strtok(NULL, ",");
            continue;
        }

        *colon = '\0';
        const char *program = token;
        int times = atoi(colon + 1);
        if (times <= 0) {
            printf("Invalid number of times for program '%s'. Skipping...\n", program);
            token = strtok(NULL, ",");
            continue;
        }

        run_program(program, times, processes, &process_count);
        token = strtok(NULL, ",");
    }

    if (process_count > 0) {
        HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) log_processes, processes, 0, NULL);
        if (thread == NULL) {
            printf("Failed to create thread for logging processes.\n");
            free(processes);
        } else {
            CloseHandle(thread);
        }
    } else {
        free(processes);
    }
}

int main() {
    char command[MAX_CMD_LENGTH];

    while (1) {
        // printf("Shell> ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0) {
            break;
        }

        parse_and_run(command);
    }

    return 0;
}
