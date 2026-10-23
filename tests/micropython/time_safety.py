import time

period = 1 << 30
assert time.ticks_add(period - 1, 1) == 0
assert time.ticks_add(3, -5) == period - 2
assert time.ticks_diff(0, period - 1) == 1
assert time.ticks_diff(period - 1, 0) == -1
assert time.ticks_diff(0, period // 2) == -(period // 2)

def refused(call):
    try:
        call()
    except (TypeError, ValueError, OverflowError):
        return
    raise AssertionError("invalid time argument accepted")

refused(lambda: time.sleep(-1))
refused(lambda: time.sleep(float("nan")))
refused(lambda: time.sleep(float("inf")))
refused(lambda: time.sleep_ms(-1))
refused(lambda: time.sleep_us(-1))
refused(lambda: time.ticks_add(period, 1))
refused(lambda: time.ticks_add(0, period // 2))
refused(lambda: time.ticks_add(0, -period // 2 - 1))
refused(lambda: time.ticks_diff(-1, 0))
refused(lambda: time.ticks_diff(0, period))
start = time.ticks_ms()
time.sleep_ms(10)
assert time.ticks_diff(time.ticks_ms(), start) >= 10
time.sleep(0)
time.sleep_us(100)
assert 0 <= time.ticks_us() < period
print("time wrap/range/delay PASS")
