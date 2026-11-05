-- An actual Lua interpreter workload, not an assembler/ELF-only check.
-- Deterministic output permits byte-for-byte comparison of GNU AS and MiniAS.
local squares = 0
for i = 1, 10 do
  squares = squares + i * i
end
assert(squares == 385)

local function counter(start)
  local n = start
  return function(step)
    n = n + step
    return n
  end
end
local next_value = counter(40)
assert(next_value(2) == 42 and next_value(5) == 47)

local sequence = { 5, 3, 9, 1, 7 }
table.sort(sequence)
assert(table.concat(sequence, ",") == "1,3,5,7,9")

local thread = coroutine.create(function(a)
  coroutine.yield(a * 2)
  return a * 3
end)
local ok, first = coroutine.resume(thread, 7)
assert(ok and first == 14)
local done, second = coroutine.resume(thread)
assert(done and second == 21 and coroutine.status(thread) == "dead")

local data = "Lua 5.5 real runtime"
assert(string.find(data, "real", 1, true) == 9)
assert(string.upper(string.sub(data, 1, 3)) == "LUA")
assert(math.floor(math.sqrt(169)) == 13)

local compiled = assert(load("return 6 * 7"))
assert(compiled() == 42)

print(string.format("MINIAS_LUA_RUNTIME squares=%d closure=%d coroutine=%d result=PASS",
                    squares, next_value(0), second))
