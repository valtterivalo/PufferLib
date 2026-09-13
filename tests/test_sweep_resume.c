#define _POSIX_C_SOURCE 200809L
#include "../src/sweep_resume.h"
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>

static void put(Ini* ini, const char* path, const char* value) {
    char section[128];
    const char* key = strrchr(path, '.');
    snprintf(section, sizeof(section), "%.*s", (int)(key - path), path);
    puf_ini_set(puf_ini_section(ini, section, 1), key + 1, value);
}

static Ini fixture(void) {
    Ini ini = {0};
    put(&ini, "sweep.objective_id", "four-bot-v1");
    put(&ini, "sweep.goal", "maximize");
    put(&ini, "sweep.metric", "net_stake");
    put(&ini, "sweep.metric_distribution", "linear");
    put(&ini, "resume.objective_id", "four-bot-v1");
    put(&ini, "resume.score", "0.125");
    put(&ini, "resume.cost", "65");
    put(&ini, "selfplay.eval_bots", "0,1,2,4");
    put(&ini, "selfplay.eval_bot_games", "256");
    put(&ini, "selfplay.eval_bot_envs", "32");
    put(&ini, "selfplay.eval_bot_threads", "4");
    put(&ini, "sweep.policy.hidden_size.distribution", "uniform_pow2");
    put(&ini, "sweep.policy.hidden_size.min", "64");
    put(&ini, "sweep.policy.hidden_size.max", "512");
    put(&ini, "sweep.policy.hidden_size.scale", "0.5");
    return ini;
}

static void rejects(const char* key, const char* value) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        Ini current = fixture(), previous = fixture();
        put(&previous, key, value);
        sweep_resume_validate(&current, &previous);
        exit(0);
    }
    int status;
    pid_t completed = waitpid(pid, &status, 0);
    assert(completed == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) != 0);
}

int main(void) {
    Ini current = fixture(), previous = fixture();
    put(&previous, "sweep.policy.hidden_size.min", "64.0");
    sweep_resume_validate(&current, &previous);
    puf_ini_free(&current);
    puf_ini_free(&previous);
    rejects("resume.objective_id", "three-bot-v1");
    rejects("sweep.goal", "minimize");
    rejects("sweep.metric", "episode_return");
    rejects("sweep.metric_distribution", "logit");
    rejects("selfplay.eval_bots", "0,1,2");
    rejects("selfplay.eval_bot_games", "128");
    rejects("selfplay.eval_bot_envs", "64");
    rejects("selfplay.eval_bot_threads", "2");
    rejects("sweep.policy.hidden_size.distribution", "log_normal");
    rejects("sweep.policy.hidden_size.min", "32");
    rejects("sweep.policy.hidden_size.max", "1024");
    rejects("sweep.policy.hidden_size.scale", "0.25");
    rejects("sweep.policy.num_layers.distribution", "int_uniform");
    rejects("resume.cost", "0");
    rejects("resume.cost", "-1");
    puts("sweep resume contract: passed");
}
