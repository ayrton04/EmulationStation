#include "scrapers/GamesDBScraper.h"

#include "utils/TimeUtil.h"
#include "FileData.h"
#include "Log.h"
#include "PlatformId.h"
#include "Settings.h"
#include "SystemData.h"
#include <jsoncpp/json/json.h>
#include <istream>
#include <string>

using namespace PlatformIds;
const std::map<PlatformId, const char*> gamesdb_platformid_map {
  { THREEDO, "25" },
  { AMIGA, "4911" },
  { AMSTRAD_CPC, "4914" },
  // missing apple2
  { ARCADE, "23" },
  // missing atari 800
  { ATARI_2600, "22" },
  { ATARI_5200, "26" },
  { ATARI_7800, "27" },
  { ATARI_JAGUAR, "28" },
  { ATARI_JAGUAR_CD, "29" },
  { ATARI_LYNX, "4924" },
  // missing atari ST/STE/Falcon
  { ATARI_XE, "30" },
  { COLECOVISION, "31" },
  { COMMODORE_64, "40" },
  { INTELLIVISION, "32" },
  { MAC_OS, "37" },
  { XBOX, "14" },
  { XBOX_360, "15" },
  { MSX, "4929" },
  { NEOGEO, "24" },
  { NEOGEO_POCKET, "4922" },
  { NEOGEO_POCKET_COLOR, "4932" },
  { NINTENDO_3DS, "4912" },
  { NINTENDO_64, "3" },
  { NINTENDO_DS, "8" },
  { FAMICOM_DISK_SYSTEM, "4936" },
  { NINTENDO_ENTERTAINMENT_SYSTEM, "7" },
  { GAME_BOY, "4" },
  { GAME_BOY_ADVANCE, "5" },
  { GAME_BOY_COLOR, "41" },
  { NINTENDO_GAMECUBE, "2" },
  { NINTENDO_WII, "9" },
  { NINTENDO_WII_U, "38" },
  { NINTENDO_VIRTUAL_BOY, "4918" },
  { NINTENDO_GAME_AND_WATCH, "4950" },
  { PC, "1" },
  { SEGA_32X, "33" },
  { SEGA_CD, "21" },
  { SEGA_DREAMCAST, "16" },
  { SEGA_GAME_GEAR, "20" },
  { SEGA_GENESIS, "18" },
  { SEGA_MASTER_SYSTEM, "35" },
  { SEGA_MEGA_DRIVE, "36" },
  { SEGA_SATURN, "17" },
  { SEGA_SG1000, "4929" },
  { PLAYSTATION, "10" },
  { PLAYSTATION_2, "11" },
  { PLAYSTATION_3, "12" },
  { PLAYSTATION_4, "4919" },
  { PLAYSTATION_VITA, "39" },
  { PLAYSTATION_PORTABLE, "13" },
  { SUPER_NINTENDO, "6" },
  { TURBOGRAFX_16, "34" }, // HuCards only
  { TURBOGRAFX_CD, "4955" }, // CD-ROMs only
  { WONDERSWAN, "4925" },
  { WONDERSWAN_COLOR, "4926" },
  { ZX_SPECTRUM, "4913" },
  { VIDEOPAC_ODYSSEY2, "4927" },
  { VECTREX, "4939" },
  { TRS80_COLOR_COMPUTER, "4941" },
  { TANDY, "4941" }
};

void thegamesdb_generate_scraper_requests(
  const ScraperSearchParams& params,
  std::queue< std::unique_ptr<ScraperRequest> >& requests,
  std::vector<ScraperSearchResult>& results)
{
  std::string path = "api.thegamesdb.net/Games/";
  bool have_game_id = false;

  static const std::string api_key = "ab00c1ddf561fe99cd36c356c34d4f0bf4b8e002018e4264ab54511e92f91402";

  std::string cleanName = params.nameOverride;
  if (!cleanName.empty() && cleanName.substr(0,3) == "id:")
  {
    std::string gameID = cleanName.substr(3);
    path += "ByGameID?apikey=" + api_key + "&id=" + HttpReq::urlEncode(gameID);
    have_game_id = true;
  }
  else
  {
    if (cleanName.empty())
      cleanName = params.game->getCleanName();
    path += "ByGameName?apikey=" + api_key + "&name=" + HttpReq::urlEncode(cleanName);
  }

  path += "&fields=" + HttpReq::urlEncode("players,publisher,genres,overview");
  path += "&include=boxart";

  if (!have_game_id)
  {
    auto& platforms = params.system->getPlatformIds();

    std::string platform_query_string;
    for(auto platformIt = platforms.cbegin(); platformIt != platforms.cend(); platformIt++)
    {
      auto map_it = gamesdb_platformid_map.find(*platformIt);
      if(map_it != gamesdb_platformid_map.cend())
      {
        platform_query_string += std::string(map_it->second) + ",";
      }
      else
      {
        LOG(LogWarning) << "TheGamesDB scraper warning - no support for platform " << getPlatformName(*platformIt);
      }
    }

    if (platform_query_string != "")
    {
      platform_query_string.pop_back();  // Get rid of extra comma
      path += "&platform=" + HttpReq::urlEncode(platform_query_string);
    }
  }

  LOG(LogInfo) << "Request URL is: " << path << "\n";

  // if we have the ID already, we don't need the GetGameList request
  // requests.push(std::unique_ptr<ScraperRequest>(new TheGamesDBRequest(results, path)));
  requests.push(std::unique_ptr<ScraperRequest>(new TheGamesDBRequest(requests, results, path)));
}

void TheGamesDBRequest::process(const std::unique_ptr<HttpReq>& req, std::vector<ScraperSearchResult>& results)
{
  assert(req->status() == HttpReq::REQ_SUCCESS);

  Json::Reader json_reader;
  Json::Value root;

  if(!json_reader.parse(req->getContent(), root))
  {
    std::string err =  "TheGamesDBRequest - Error parsing json. \n";
    setError(err);
    LOG(LogError) << err;
    return;
  }

  if (root["code"].asInt() != 200)
  {
    return;
  }

  const auto& data = root["data"];
  const auto& include = root["include"];

  auto count = data["count"].asInt();
  const auto& games = data["games"];

  const auto& boxart = include["boxart"];
  const auto& base_image_url = boxart["base_url"];
  const auto base_thumb_url = base_image_url["thumb"].asString();
  const auto base_full_url = base_image_url["original"].asString();
  const auto& image_data = boxart["data"];

  ScraperSearchResult result;

  auto set_string_if_present = [](
    const Json::Value& value,
    const std::string value_key,
    const std::string result_key,
    ScraperSearchResult& result)
  {
    const auto resolved = value[value_key];

    if (!resolved.isNull())
    {
      result.mdl.set(result_key, value[value_key].asString());
    }
  };

  for (int i = 0; i < static_cast<int>(games.size()); ++i)
  {
    const auto& game = games[i];
    const auto game_id = game["id"].asString();

    set_string_if_present(game, "game_title", "name", result);
    set_string_if_present(game, "overview", "desc", result);
    set_string_if_present(game, "players", "players", result);

    const auto& release_date = game["release_date"];
    if (!release_date.isNull())
    {
      result.mdl.set("releasedate",
        Utils::Time::DateTime(Utils::Time::stringToTime(game["release_date"].asString(), "%Y-%m-%d")));
    }

    //result.mdl.set("developer", game.child("Developer").text().get());
    //result.mdl.set("publisher", game.child("Publisher").text().get());
    //result.mdl.set("genre", game.child("Genres").first_child().text().get());

    const auto& game_image_info = image_data[game_id];

    if (!game_image_info.isNull() && game_image_info.size() > 0)
    {
      auto side = game_image_info[0];

      if (side["side"] == "back" && game_image_info.size() > 1)
      {
        side = game_image_info[1];
      }

      const auto path = side["filename"].asString();

      result.thumbnailUrl = base_thumb_url + path;
      result.imageUrl = base_full_url + path;
    }

    results.push_back(result);
  }

}
