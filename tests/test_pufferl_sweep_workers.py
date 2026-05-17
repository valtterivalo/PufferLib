import inspect

from pufferlib import pufferl


def test_sweep_does_not_spawn_new_worker_after_silent_timeout():
    source = inspect.getsource(pufferl.sweep)

    assert "PUFFER_SWEEP_WORKER_TIMEOUT" not in source
    assert "result_queue.get(timeout=" not in source
    assert "queue.Empty" not in source
