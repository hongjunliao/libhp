--[[
this file is part of xhmdm project
@author hongjun.liao <docici@126.com>, @date 2020/7/7

Redis Pub/Sub system:
  return off-line messages

usage: 
eval SHA1 0 session
e.g.
evalsha 456be53be0c61603929c31c2895af5340e3b2d30 0 xhmdm_test/s/865452044887154

--]]
local rst={};
local k = nil;
local j = 1;
local s=redis.call('hgetall', ARGV[1]);
for i,v in pairs(s) do
  if((i % 2) == 0) then
    local c = redis.pcall('ZRANGEBYSCORE', k, v + 1, "+inf", "WITHSCORES");
    if not c.err then
      rst[j]=k;
      rst[j+1]=c;
      j = j + 2;
    end
  else k = v;
  end
end
return rst

