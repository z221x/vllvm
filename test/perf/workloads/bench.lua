-- lua 性能负载：n-body 数值积分（AU/年单位，G=4π²，圆轨道稳定）+ 字符串/表操作。
local function nbody(n)
  local G = 4 * math.pi * math.pi
  local bodies = { {0, 0, 0, 0, 0, 0, 1} }
  -- {半径, 质量, 初始相位}
  local defs = { {0.4, 2e-6, 0.3}, {1.0, 3e-5, 2.1},
                 {5.2, 9.5e-4, 4.0}, {30.1, 5.2e-5, 5.5} }
  for k, d in ipairs(defs) do
    local r, m, ph = d[1], d[2], d[3]
    local v = math.sqrt(G / r)
    bodies[k + 1] = { r * math.cos(ph), r * math.sin(ph), 0,
                      -v * math.sin(ph), v * math.cos(ph), 0, m }
  end
  local nb = #bodies
  local function advance(dt)
    for i = 1, nb do
      for j = i + 1, nb do
        local a, b = bodies[i], bodies[j]
        local dx, dy = a[1] - b[1], a[2] - b[2]
        local dist2 = dx * dx + dy * dy
        local dist = math.sqrt(dist2)
        local mag = dt / (dist2 * dist)
        local mm, nm = G * a[7] * mag, G * b[7] * mag
        a[4], a[5] = a[4] - dx * nm, a[5] - dy * nm
        b[4], b[5] = b[4] + dx * mm, b[5] + dy * mm
      end
    end
    for i = 1, nb do
      local p = bodies[i]
      p[1], p[2] = p[1] + dt * p[4], p[2] + dt * p[5]
    end
  end
  local function energy()
    local e = 0
    for i = 1, nb do
      local p = bodies[i]
      e = e + 0.5 * p[7] * (p[4] * p[4] + p[5] * p[5])
      for j = i + 1, nb do
        local q = bodies[j]
        local dx, dy = p[1] - q[1], p[2] - q[2]
        e = e - G * p[7] * q[7] / math.sqrt(dx * dx + dy * dy)
      end
    end
    return e
  end
  for _ = 1, n do
    advance(0.002)
  end
  return string.format("%.6f", energy())
end

local function strstress(n)
  local acc = 0
  local t = {}
  for i = 1, n do
    local s = string.format("%s-%d-%x", "vllvm", i, i * 2654435761 % 0x10ffff)
    t[#t + 1] = s:upper():sub(1, 12)
    acc = (acc + #s * i) % 0xffffffff
    if #t > 64 then
      table.concat(t, ",")
      t = {}
    end
  end
  return acc
end

print("nbody " .. nbody(1500000))
print("str " .. strstress(150000))
