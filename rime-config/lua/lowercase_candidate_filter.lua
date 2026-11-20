-- Keep each custom English entry together in display order. Other pure-letter
-- candidates keep their existing lowercase variant.
local function emit(cand, text, seen)
  if seen[text] then return end
  seen[text] = true
  if text == cand.text then
    yield(cand)
  else
    yield(ShadowCandidate(cand, cand.type, text, cand.comment))
  end
end

return function(input)
  local seen = {}
  for cand in input:iter() do
    local text = cand.text
    if cand.type == "custom_english" then
      emit(cand, text, seen)
      emit(cand, text:lower(), seen)
      emit(cand, text:upper(), seen)
    elseif not seen[text] then
      yield(cand)
      if text:match("^[A-Za-z]+$") then
        local lower = text:lower()
        if lower ~= text and not seen[lower] then
          yield(ShadowCandidate(cand, cand.type, lower, cand.comment))
        end
      end
    end
  end
end
