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
#include "../RpcUtils.h"

#include "../../../GameState.h"
#include "../../../actions/GameActionResult.h"
#include "../../../actions/ParkSetLoanAction.h"
#include "../../../core/EnumUtils.hpp"
#include "../../../core/Money.hpp"
#include "../../../interface/WindowBase.h"
#include "../../../management/Finance.h"
#include "../../../telemetry/AIAgentActivityFeed.h"
#include "../../../world/ParkData.h"

#include <array>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For kError* constants

    namespace
    {
        struct FinanceCategoryDescriptor
        {
            ExpenditureType type;
            const char* key;
            const char* label;
            bool income;
        };

        constexpr std::array<FinanceCategoryDescriptor, 14> kFinanceCategories = {
            FinanceCategoryDescriptor{ ExpenditureType::rideConstruction, "rideConstruction", "Ride Construction",
                                       false },
            FinanceCategoryDescriptor{ ExpenditureType::rideRunningCosts, "rideRunning", "Ride Running Costs", false },
            FinanceCategoryDescriptor{ ExpenditureType::landPurchase, "landPurchase", "Land Purchase", false },
            FinanceCategoryDescriptor{ ExpenditureType::landscaping, "landscaping", "Landscaping", false },
            FinanceCategoryDescriptor{ ExpenditureType::parkEntranceTickets, "parkEntrance", "Park Entrance Tickets",
                                       true },
            FinanceCategoryDescriptor{ ExpenditureType::parkRideTickets, "rideTickets", "Ride Tickets", true },
            FinanceCategoryDescriptor{ ExpenditureType::shopSales, "shopSales", "Shop Sales", true },
            FinanceCategoryDescriptor{ ExpenditureType::shopStock, "shopStock", "Shop Stock", false },
            FinanceCategoryDescriptor{ ExpenditureType::foodDrinkSales, "foodDrinkSales", "Food / Drink Sales", true },
            FinanceCategoryDescriptor{ ExpenditureType::foodDrinkStock, "foodDrinkStock", "Food / Drink Stock", false },
            FinanceCategoryDescriptor{ ExpenditureType::wages, "wages", "Staff Wages", false },
            FinanceCategoryDescriptor{ ExpenditureType::marketing, "marketing", "Marketing", false },
            FinanceCategoryDescriptor{ ExpenditureType::research, "research", "Research", false },
            FinanceCategoryDescriptor{ ExpenditureType::interest, "interest", "Loan Interest", false },
        };

        money64 SumMonthlyFinance(const Park::ParkData& park, size_t monthIndex)
        {
            money64 total = 0;
            if (monthIndex >= kExpenditureTableMonthCount)
            {
                return total;
            }
            for (size_t i = 0; i < static_cast<size_t>(ExpenditureType::count); ++i)
            {
                total += park.expenditureTable[monthIndex][i];
            }
            return total;
        }

        json_t BuildFinanceCategoriesPayload()
        {
            const auto& park = getGameState().park;
            json_t categories = json_t::array();
            for (const auto& descriptor : kFinanceCategories)
            {
                const auto idx = EnumValue(descriptor.type);
                json_t entry = json_t::object();
                entry["key"] = descriptor.key;
                entry["label"] = descriptor.label;
                entry["income"] = descriptor.income;
                entry["thisMonth"] = MoneyToDouble(park.expenditureTable[0][idx]);
                entry["lastMonth"] = kExpenditureTableMonthCount > 1 ? MoneyToDouble(park.expenditureTable[1][idx]) : 0.0;

                json_t history = json_t::array();
                for (size_t month = 0; month < kExpenditureTableMonthCount; ++month)
                {
                    json_t record = json_t::object();
                    record["monthOffset"] = static_cast<int32_t>(month);
                    record["value"] = MoneyToDouble(park.expenditureTable[month][idx]);
                    history.push_back(record);
                }
                entry["history"] = history;
                categories.push_back(entry);
            }
            return categories;
        }

        json_t BuildFinanceSummaryPayload()
        {
            const auto& park = getGameState().park;

            json_t payload = json_t::object();
            payload["cash"] = MoneyToDouble(park.cash);
            payload["loan"] = MoneyToDouble(park.bankLoan);
            payload["loanMax"] = MoneyToDouble(park.maxBankLoan);
            payload["interestRate"] = park.bankLoanInterestRate;
            payload["parkValue"] = MoneyToDouble(park.value);
            payload["companyValue"] = MoneyToDouble(park.companyValue);

            json_t profit = json_t::object();
            profit["thisMonth"] = MoneyToDouble(SumMonthlyFinance(park, 0));
            profit["lastMonth"] = MoneyToDouble(SumMonthlyFinance(park, 1));
            payload["operatingProfit"] = profit;

            json_t marketing = json_t::object();
            marketing["thisMonth"] = MoneyToDouble(park.expenditureTable[0][EnumValue(ExpenditureType::marketing)]);
            marketing["lastMonth"] = MoneyToDouble(park.expenditureTable[1][EnumValue(ExpenditureType::marketing)]);
            payload["marketingSpend"] = marketing;

            json_t research = json_t::object();
            research["thisMonth"] = MoneyToDouble(park.expenditureTable[0][EnumValue(ExpenditureType::research)]);
            research["lastMonth"] = MoneyToDouble(park.expenditureTable[1][EnumValue(ExpenditureType::research)]);
            payload["researchSpend"] = research;

            payload["categories"] = BuildFinanceCategoriesPayload();
            return payload;
        }

        json_t BuildFinanceHistoryPayload()
        {
            const auto& park = getGameState().park;
            json_t cashHistory = json_t::array();
            json_t profitHistory = json_t::array();
            json_t valueHistory = json_t::array();

            for (size_t i = 0; i < kFinanceHistorySize; ++i)
            {
                if (park.cashHistory[i] != kMoney64Undefined)
                {
                    json_t record = json_t::object();
                    record["index"] = static_cast<int32_t>(i);
                    record["value"] = MoneyToDouble(park.cashHistory[i]);
                    cashHistory.push_back(record);
                }
                if (park.weeklyProfitHistory[i] != kMoney64Undefined)
                {
                    json_t record = json_t::object();
                    record["index"] = static_cast<int32_t>(i);
                    record["value"] = MoneyToDouble(park.weeklyProfitHistory[i]);
                    profitHistory.push_back(record);
                }
                if (park.valueHistory[i] != kMoney64Undefined)
                {
                    json_t record = json_t::object();
                    record["index"] = static_cast<int32_t>(i);
                    record["value"] = MoneyToDouble(park.valueHistory[i]);
                    valueHistory.push_back(record);
                }
            }

            json_t payload = json_t::object();
            payload["cashHistory"] = cashHistory;
            payload["weeklyProfitHistory"] = profitHistory;
            payload["parkValueHistory"] = valueHistory;
            return payload;
        }

        json_t BuildLoanStatusPayload()
        {
            const auto& park = getGameState().park;
            json_t payload = json_t::object();
            payload["loan"] = MoneyToDouble(park.bankLoan);
            payload["loanMax"] = MoneyToDouble(park.maxBankLoan);
            payload["cash"] = MoneyToDouble(park.cash);
            payload["interestRate"] = park.bankLoanInterestRate;
            payload["companyValue"] = MoneyToDouble(park.companyValue);
            payload["parkValue"] = MoneyToDouble(park.value);
            double loanToValue = 0.0;
            if (park.companyValue != 0)
            {
                loanToValue = MoneyToDouble(park.bankLoan) / MoneyToDouble(park.companyValue);
            }
            payload["loanToValue"] = loanToValue;
            return payload;
        }

        Telemetry::AIAgentFollowHint MakeFinancesHint(std::string_view method, std::string contextLabel)
        {
            Telemetry::AIAgentFollowHint hint;
            hint.sourceMethod = std::string(method);
            hint.contextLabel = std::move(contextLabel);
            hint.requestCameraFocus = false;
            Telemetry::GenericWindowIntent intent;
            intent.windowClass = WindowClass::finances;
            hint.windowIntent = intent;
            return hint;
        }

        RpcResult HandleFinanceSummary(const json_t& /*params*/)
        {
            auto payload = BuildFinanceSummaryPayload();
            auto hint = MakeFinancesHint("finance.status", "Viewed finance status");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        RpcResult HandleFinanceHistory(const json_t& /*params*/)
        {
            auto payload = BuildFinanceHistoryPayload();
            auto hint = MakeFinancesHint("finance.history", "Viewed finance history");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        RpcResult HandleLoanStatus(const json_t& /*params*/)
        {
            auto payload = BuildLoanStatusPayload();
            auto hint = MakeFinancesHint("loans.status", "Viewed loan status");
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        RpcResult HandleLoanSet(const json_t& params)
        {
            if (!params.is_object())
            {
                return RpcResult::Error(kErrorInvalidParams, "Params must be a JSON object");
            }
            auto target = GetDoubleParam(params, "value");
            if (!target)
            {
                return RpcResult::Error(kErrorInvalidParams, "value is required");
            }

            auto desiredLoan = DoubleToMoney(*target);
            // Capture current loan and cash before action
            const auto& park = getGameState().park;
            auto previousLoan = park.bankLoan;
            auto previousCash = park.cash;

            auto action = GameActions::ParkSetLoanAction(desiredLoan);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.Error != GameActions::Status::Ok)
            {
                return RpcResult::Error(kErrorActionFailed, BuildGameActionErrorMessage(result));
            }
            json_t payload = BuildLoanStatusPayload();
            // Override loan and cash values since game state may not be updated yet
            // Cash changes by the loan delta: decrease loan = pay from cash, increase loan = receive cash
            auto expectedCash = previousCash + (desiredLoan - previousLoan);
            payload["loan"] = MoneyToDouble(desiredLoan);
            payload["cash"] = MoneyToDouble(expectedCash);
            std::string contextLabel = "Set park loan to " + FormatMoneyString(desiredLoan);
            auto hint = MakeFinancesHint("loans.set", std::move(contextLabel));
            return RpcResult::Ok(std::move(payload), std::move(hint));
        }

        // Static registration
        struct FinanceHandlerRegistrar
        {
            FinanceHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("finance.status", HandleFinanceSummary);
                registry.Register("finance.history", HandleFinanceHistory);
                registry.Register("loans.status", HandleLoanStatus);
                registry.Register("loans.set", HandleLoanSet);
            }
        } financeRegistrar;

    } // namespace

    void InitFinanceHandlers()
    {
        (void)financeRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING
