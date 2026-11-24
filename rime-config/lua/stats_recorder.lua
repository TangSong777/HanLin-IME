-- 打字统计记录器（可选功能，默认关闭）
--
-- 启用方法：在 rime_ice.custom.yaml 的 engine/processors 里加一行
--   - lua_processor@*stats_recorder
-- 关闭方法：删掉那一行，或把 stats_enabled 设为 false。
--
-- 记录文件：<userdir>/stats.log，每行 "YYYY-MM-DD\t字数"。
-- 只在提交候选时追加一行，不参与候选生成，对输入延迟影响可忽略。

local stats_recorder = {}

local function stats_path()
    local user_dir = rime_api.get_user_data_dir()
    return user_dir .. "/stats.log"
end

local function record(n)
    local f = io.open(stats_path(), "a")
    if not f then return end
    f:write(os.date("%Y-%m-%d") .. "\t" .. n .. "\n")
    f:close()
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
