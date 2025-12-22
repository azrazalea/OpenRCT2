/*****************************************************************************
 * Copyright (c) 2014-2025 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

#include "../HandlerRegistry.h"
#include "HandlerInit.h"
#include "../RpcTypes.h"

#include "../../../Date.h"
#include "../../../GameState.h"
#include "../../../interface/WindowBase.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../world/Climate.h"

#include <array>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For shared types and utilities

    namespace
    {
        constexpr int32_t kWeatherTransitionTicks = 1920;

        constexpr std::array<const char*, MONTH_COUNT> kMonthNames = {
            "March",
            "April",
            "May",
            "June",
            "July",
            "August",
            "September",
            "October",
        };

        std::string_view GetMonthName(int32_t monthIndex)
        {
            if (monthIndex < 0)
            {
                return kMonthNames[0];
            }
            return kMonthNames[monthIndex % static_cast<int32_t>(kMonthNames.size())];
        }

        std::string_view WeatherTypeToString(WeatherType type)
        {
            switch (type)
            {
                case WeatherType::Sunny:
                    return "sunny";
                case WeatherType::PartiallyCloudy:
                    return "partlyCloudy";
                case WeatherType::Cloudy:
                    return "cloudy";
                case WeatherType::Rain:
                    return "rain";
                case WeatherType::HeavyRain:
                    return "heavyRain";
                case WeatherType::Thunder:
                    return "thunder";
                case WeatherType::Snow:
                    return "snow";
                case WeatherType::HeavySnow:
                    return "heavySnow";
                case WeatherType::Blizzard:
                    return "blizzard";
                default:
                    return "unknown";
            }
        }

        std::string_view WeatherEffectToString(WeatherEffectType effect)
        {
            switch (effect)
            {
                case WeatherEffectType::None:
                    return "none";
                case WeatherEffectType::Rain:
                    return "rain";
                case WeatherEffectType::Storm:
                    return "storm";
                case WeatherEffectType::Snow:
                    return "snow";
                case WeatherEffectType::Blizzard:
                    return "blizzard";
                default:
                    return "none";
            }
        }

        std::string_view WeatherLevelToString(WeatherLevel level)
        {
            switch (level)
            {
                case WeatherLevel::None:
                    return "none";
                case WeatherLevel::Light:
                    return "light";
                case WeatherLevel::Heavy:
                    return "heavy";
                default:
                    return "none";
            }
        }

        std::string_view SeasonFromMonth(int32_t month)
        {
            switch (month)
            {
                case MONTH_MARCH:
                case MONTH_APRIL:
                    return "spring";
                case MONTH_MAY:
                case MONTH_JUNE:
                    return "summer";
                case MONTH_JULY:
                case MONTH_AUGUST:
                    return "lateSummer";
                case MONTH_SEPTEMBER:
                    return "autumn";
                case MONTH_OCTOBER:
                    return "lateAutumn";
                default:
                    return "season";
            }
        }

        json_t BuildWeatherStateObject(const WeatherState& state)
        {
            json_t node = json_t::object();
            node["type"] = WeatherTypeToString(state.weatherType);
            node["effect"] = WeatherEffectToString(state.weatherEffect);
            node["level"] = WeatherLevelToString(state.level);
            node["temperatureC"] = state.temperature;
            node["temperatureF"] = ClimateCelsiusToFahrenheit(state.temperature);
            node["gloom"] = state.weatherGloom;
            return node;
        }

        json_t BuildWeatherStatusPayload()
        {
            const auto& gameState = getGameState();
            json_t payload = json_t::object();
            payload["current"] = BuildWeatherStateObject(gameState.weatherCurrent);
            payload["forecast"] = BuildWeatherStateObject(gameState.weatherNext);
            payload["ticksUntilChange"] = gameState.weatherUpdateTimer;
            payload["transitionProgress"] =
                static_cast<double>(gameState.weatherUpdateTimer) / static_cast<double>(kWeatherTransitionTicks);
            payload["season"] = SeasonFromMonth(gameState.date.GetMonth());
            // Convert from 0-indexed internal representation to 1-indexed user-facing values
            // to match what the game UI displays
            payload["month"] = gameState.date.GetMonth() + 1;
            payload["monthName"] = GetMonthName(gameState.date.GetMonth());
            payload["day"] = gameState.date.GetDay() + 1;
            payload["year"] = gameState.date.GetYear() + 1;
            payload["dayProgress"] = static_cast<double>(gameState.date.monthTicks) / static_cast<double>(kTicksPerMonth);
            payload["isRaining"] = ClimateIsRaining();
            payload["isSnowing"] = ClimateIsSnowing();
            payload["isSnowingHeavily"] = ClimateIsSnowingHeavily();
            payload["hasPrecipitation"] = ClimateHasWeatherEffect();
            payload["freezeCheatEnabled"] = gameState.cheats.freezeWeather;
            payload["umbrellaDemand"] = ClimateIsRaining() || gameState.weatherNext.weatherEffect == WeatherEffectType::Rain;
            payload["stormAlert"] = gameState.weatherCurrent.weatherEffect == WeatherEffectType::Storm;
            return payload;
        }

        json_t BuildWeatherForecastPayload()
        {
            const auto& gameState = getGameState();
            json_t payload = json_t::object();
            payload["next"] = BuildWeatherStateObject(gameState.weatherNext);
            payload["ticksUntilChange"] = gameState.weatherUpdateTimer;
            payload["transitionProgress"] =
                static_cast<double>(gameState.weatherUpdateTimer) / static_cast<double>(kWeatherTransitionTicks);
            payload["freezeCheatEnabled"] = gameState.cheats.freezeWeather;
            return payload;
        }

        Telemetry::AIAgentFollowHint MakeWeatherHint(std::string_view method, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.requestCameraFocus = false;
            Telemetry::GenericWindowIntent intent;
            intent.windowClass = WindowClass::parkInformation;
            hint.windowIntent = intent;
            return hint;
        }

        RpcResult HandleWeatherStatus(const json_t& /*params*/)
        {
            auto payload = BuildWeatherStatusPayload();
            auto hint = MakeWeatherHint("weather.status", "Viewed weather status");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        RpcResult HandleWeatherForecast(const json_t& /*params*/)
        {
            auto payload = BuildWeatherForecastPayload();
            auto hint = MakeWeatherHint("weather.forecast", "Viewed weather forecast");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        // Static registration
        struct WeatherHandlerRegistrar
        {
            WeatherHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("weather.status", HandleWeatherStatus);
                registry.Register("weather.forecast", HandleWeatherForecast);
            }
        } weatherRegistrar;

    } // namespace

    void InitWeatherHandlers()
    {
        (void)weatherRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
