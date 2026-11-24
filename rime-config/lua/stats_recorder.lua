-- 打字统计记录器（可选功能，默认关闭）
--
-- 启用方法：在 rime_ice.custom.yaml 的 engine/processors 里加一行
--   - lua_processor@*stats_recorder
-- 关闭方法：删掉那一行，或把 stats_enabled 设为 false。
--
-- 记录文件：<userdir>/stats.log，每天一行 "YYYY-MM-DD\t字数"。
-- 当天重复上屏会累加到最后一行，不会无限增长。

local stats_recorder = {}

local function stats_path()
    local user_dir = rime_api.get_user_data_dir()
    return user_dir .. "/stats.log"
end

local function record(n)
    local path = stats_path()
    local today = os.date("%Y-%m-%d")
    local f = io.open(path, "r")
    local content = ""
    if f then
        content = f:read("*a") or ""
        f:close()
    end
    -- 去掉末尾换行，避免最后一行匹配到空串
    local body = content:gsub("\n$", "")
    local last_line = body:match("([^\n]+)$") or ""
    local d, c = last_line:match("^(%d%d%d%d%-%d%d%-%d%d)\t(%d+)$")
    if d == today then
        local prefix = body:sub(1, #body - #last_line)
        local w = io.open(path, "w")
        if w then
            w:write(prefix .. today .. "\t" .. (tonumber(c) + n) .. "\n")
            w:close()
        end
    else
        local a = io.open(path, "a")
        if a then
            a:write(today .. "\t" .. n .. "\n")
            a:close()
        end
    end
end

function stats_recorder.func(key, env)
    if not (env.engine.schema.config:get_bool("stats_enabled") or false) then return 2 end
    local context = env.engine.context
    if not context:is_composing() then return 2 end
    local repr = key:repr()
    local is_commit = (repr == "space" or repr == "Return" or repr:match("^[1-9]$"))
    if not is_commit then return 2 end
    local cand = context:get_selected_candidate()
    if cand then
        local text = cand.text or ""
        local n = utf8.len(text) or #text
        if n and n > 0 then record(n) end
    end
    return 2
end

return stats_recorder
