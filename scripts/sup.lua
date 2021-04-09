--[[
this file is part of xhmdm project
@author hongjun.liao <docici@126.com>, @date 2020/7/7

import to Redis: 
redis-cli -h 10.248.97.237 -p 6010 -a redis@pass script load "$(cat scripts/file.lua)"

Redis Pub/Sub system:
  update session

usage: 
eval SHA1 n_topics topics session
e.g.
evalsha 7dfb24710befe978f25be0846ae1b50bb772d799 3 xhmdm_test:1:865452044887154 xhmdm_test:apps:chat.tox.antox xhmdm_test:dept:1006 xhmdm_test:s:865452044887154

--]]

local k = nil;
local keys={};
local s=redis.call('hgetall', ARGV[1]);
for i,v in pairs(KEYS) do
   keys[v] = true;

   -- set score to lastest
   local score = 0;
   local c = redis.call('zrevrange', v, 0, 0, 'withscores');
   if c[2] then
     score = c[2];
   end

   -- insert new using hsetnx
   redis.call('hsetnx', ARGV[1], v, score);
end
for i,v in pairs(s) do
   if((i % 2) == 0) then
     if not keys[k] then
       -- remove old ones
       redis.call('hdel', ARGV[1], k);
     end
  else k = v;
  end
end
return 1;


