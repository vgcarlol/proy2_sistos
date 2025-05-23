// sim.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAX_PID_LEN 16

typedef struct {
    char    pid[MAX_PID_LEN];
    int     bt, at, prio;
    int     remaining;
    int     completion_time;
    int     waiting_time;
} Process;

// --- Parser de procesos ---
int load_processes(const char *filename, Process **procs, int *n) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;
    Process *arr = NULL;
    int cap = 0, cnt = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='\n' || line[0]=='#') continue;
        if (cnt >= cap) {
            cap = cap ? cap*2 : 8;
            Process *tmp = realloc(arr, cap * sizeof(Process));
            if (!tmp) { free(arr); fclose(f); return -1; }
            arr = tmp;
        }
        if (sscanf(line, " %15[^,], %d, %d, %d",
                   arr[cnt].pid,
                   &arr[cnt].bt,
                   &arr[cnt].at,
                   &arr[cnt].prio) != 4) {
            free(arr);
            fclose(f);
            return -1;
        }
        arr[cnt].remaining = arr[cnt].bt;
        cnt++;
    }
    fclose(f);
    *procs = arr;
    *n = cnt;
    return 0;
}

// --- Cálculo de métrica ---
double calc_avg_waiting(Process *p, int n) {
    double sum = 0;
    for (int i = 0; i < n; i++) {
        sum += p[i].waiting_time;
    }
    return sum / n;
}

// --- FIFO ---
void run_fifo(Process *orig, int n) {
    Process *p = malloc(n * sizeof(Process));
    memcpy(p, orig, n * sizeof(Process));
    // ordenar por arrival time
    for (int i = 0; i < n; i++)
        for (int j = i+1; j < n; j++)
            if (p[i].at > p[j].at) {
                Process tmp = p[i]; p[i] = p[j]; p[j] = tmp;
            }

    int time = 0;
    printf("Gantt (start:dur)=PID\n");
    for (int i = 0; i < n; i++) {
        if (time < p[i].at) time = p[i].at;
        printf(" [%2d:%2d]=%s", time, p[i].bt, p[i].pid);
        time += p[i].bt;
        p[i].completion_time = time;
        p[i].waiting_time = p[i].completion_time - p[i].at - p[i].bt;
    }
    printf("\nAverage waiting time: %.2f cycles\n",
           calc_avg_waiting(p, n));
    free(p);
}

// --- SJF (no-preemptivo) ---
void run_sjf(Process *orig, int n) {
    Process *p = malloc(n * sizeof(Process));
    memcpy(p, orig, n * sizeof(Process));
    int done = 0, time = 0;
    int *used = calloc(n, sizeof(int));

    printf("Gantt (start:dur)=PID\n");
    while (done < n) {
        int idx = -1, minbt = INT_MAX;
        for (int i = 0; i < n; i++) {
            if (!used[i] && p[i].at <= time && p[i].bt < minbt) {
                minbt = p[i].bt;
                idx = i;
            }
        }
        if (idx < 0) { time++; continue; }
        printf(" [%2d:%2d]=%s", time, p[idx].bt, p[idx].pid);
        time += p[idx].bt;
        p[idx].completion_time = time;
        p[idx].waiting_time = p[idx].completion_time - p[idx].at - p[idx].bt;
        used[idx] = 1;
        done++;
    }
    printf("\nAverage waiting time: %.2f cycles\n",
           calc_avg_waiting(p, n));
    free(p);
    free(used);
}

// --- SRT (preemptivo) ---
void run_srt(Process *orig, int n) {
    Process *p = malloc(n * sizeof(Process));
    memcpy(p, orig, n * sizeof(Process));
    int done = 0, time = 0;

    printf("Gantt (start:dur)=PID\n");
    while (done < n) {
        int idx = -1, minrem = INT_MAX;
        for (int i = 0; i < n; i++) {
            if (p[i].remaining > 0 && p[i].at <= time && p[i].remaining < minrem) {
                minrem = p[i].remaining;
                idx = i;
            }
        }
        if (idx < 0) { time++; continue; }
        printf(" [%2d: 1]=%s", time, p[idx].pid);
        p[idx].remaining--;
        time++;
        if (p[idx].remaining == 0) {
            p[idx].completion_time = time;
            p[idx].waiting_time =
                p[idx].completion_time - p[idx].at - p[idx].bt;
            done++;
        }
    }
    printf("\nAverage waiting time: %.2f cycles\n",
           calc_avg_waiting(p, n));
    free(p);
}

// --- Round Robin (escaneo circular) ---
void run_rr(Process *orig, int n, int quantum) {
    Process *p = malloc(n * sizeof(Process));
    memcpy(p, orig, n * sizeof(Process));
    int done = 0, time = 0, last = -1;

    printf("Gantt (start:dur)=PID\n");
    while (done < n) {
        int idx = -1;
        for (int step = 1; step <= n; step++) {
            int j = (last + step) % n;
            if (p[j].remaining > 0 && p[j].at <= time) {
                idx = j;
                break;
            }
        }
        if (idx < 0) { time++; continue; }
        int dur = p[idx].remaining < quantum ? p[idx].remaining : quantum;
        printf(" [%2d:%2d]=%s", time, dur, p[idx].pid);
        p[idx].remaining -= dur;
        time += dur;
        if (p[idx].remaining == 0) {
            p[idx].completion_time = time;
            done++;
        }
        last = idx;
    }
    printf("\n");
    // calcular tiempos de espera
    for (int i = 0; i < n; i++) {
        p[i].waiting_time = p[i].completion_time - p[i].at - p[i].bt;
    }
    printf("Average waiting time: %.2f cycles\n", calc_avg_waiting(p, n));
    free(p);
}

// --- Priority (no-preemptivo) ---
void run_prio(Process *orig, int n) {
    Process *p = malloc(n * sizeof(Process));
    memcpy(p, orig, n * sizeof(Process));
    int done = 0, time = 0;
    int *used = calloc(n, sizeof(int));

    printf("Gantt (start:dur)=PID\n");
    while (done < n) {
        int idx = -1, best = INT_MAX;
        for (int i = 0; i < n; i++) {
            if (!used[i] && p[i].at <= time && p[i].prio < best) {
                best = p[i].prio;
                idx = i;
            }
        }
        if (idx < 0) { time++; continue; }
        printf(" [%2d:%2d]=%s", time, p[idx].bt, p[idx].pid);
        time += p[idx].bt;
        p[idx].completion_time = time;
        p[idx].waiting_time = p[idx].completion_time - p[idx].at - p[idx].bt;
        used[idx] = 1;
        done++;
    }
    printf("\nAverage waiting time: %.2f cycles\n",
           calc_avg_waiting(p, n));
    free(p);
    free(used);
}

// --- main ---
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
            "Uso: %s <FIFO|SJF|SRT|RR|PRIO> <input.txt> [quantum]\n",
            argv[0]);
        return 1;
    }
    char *alg     = argv[1];
    char *file    = argv[2];
    int   quantum = (argc >= 4 ? atoi(argv[3]) : 1);

    Process *procs;
    int n;
    if (load_processes(file, &procs, &n) != 0) {
        fprintf(stderr, "Error leyendo %s\n", file);
        return 1;
    }

    if      (strcmp(alg, "FIFO") == 0) run_fifo(procs, n);
    else if (strcmp(alg, "SJF")  == 0) run_sjf(procs, n);
    else if (strcmp(alg, "SRT")  == 0) run_srt(procs, n);
    else if (strcmp(alg, "RR")   == 0) run_rr(procs, n, quantum);
    else if (strcmp(alg, "PRIO") == 0) run_prio(procs, n);
    else {
        fprintf(stderr, "Algoritmo desconocido: %s\n", alg);
        free(procs);
        return 1;
    }

    free(procs);
    return 0;
}
