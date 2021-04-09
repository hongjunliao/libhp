--[[
this file is part of xhmdm project
@author hongjun.liao <docici@126.com>, @date 2020/10/21

import to Redis: 
redis-cli -h 10.248.97.237 -p 6010 -a redis@pass script load "$(cat scripts/file.lua)"

Redis Pub/Sub system:
  update session: add key(s, topics) to session

usage: 
eval SHA1 n_topics topics session
e.g.
evalsha 02c65a4e190d212bc624624870f42337481e670d 2 xhmdm_test:group:usb xhmdm_test:group::wifi xhmdm_test:s:865452044887154

--]]

local s=redis.call('hgetall', ARGV[1]);
for i,v in pairs(KEYS) do
   -- set score to lastest
   local score = 0;
   local c = redis.call('zrevrange', v, 0, 0, 'withscores');
   if c[2] then
     score = c[2];
   end

   -- insert new using hsetnx
   redis.call('hsetnx', ARGV[1], v, score);
end
return 1;


