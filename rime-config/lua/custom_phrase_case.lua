-- Query the existing Rime table with a folded English code. Keep the user's
-- spelling in the preedit and let the normal table handle other phrases.
local M = {}
local english_code = "^[A-Za-z][A-Za-z0-9]*$"
local strong_prefix_length, strong_prefix_boost = 5, 50

local function is_english_word(text)
  return text:match("^[A-Za-z0-9]+$") and text:find("[A-Za-z]")
end

function M.init(env)
  local name = env.name_space
  env.table = Component.Translator(env.engine, "", "table_translator@" .. name)
  env.completion = env.engine.schema.config:get_bool(name .. "/enable_completion")
end

local function query(code, input, seg, env, english, other)
  local result = env.table:query(code, seg)
  if not result then return end
  local can_fold = input:match(english_code)
  for cand in result:iter() do
    local completion = cand.type == "completion"
    if not env.completion or completion then
      if is_english_word(cand.text) and can_fold then
        if english and (not completion or #input > 1) then
          local word = Candidate("custom_english", cand.start, cand._end, cand.text, cand.comment)
          word.preedit = input
          word.quality = cand.quality
          if completion and #input >= strong_prefix_length then
            word.quality = word.quality + strong_prefix_boost
          end
          yield(word)
        end
      elseif other then
        yield(cand)
      end
    end
  end
end

function M.func(input, seg, env)
  if not env.table or seg:has_tag("calculator") then return end
  if not input:match(english_code) then
    query(input, input, seg, env, false, true)
    return
  end
  local folded = input:lower()
  if input ~= folded then
    query(input, input, seg, env, false, true)
  end
  query(folded, input, seg, env, true, input == folded)
end

return M
