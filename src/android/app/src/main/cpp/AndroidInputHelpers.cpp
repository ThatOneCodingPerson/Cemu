#include "AndroidInputHelpers.h"

#include "input/ControllerFactory.h"
#include "input/emulated/ClassicController.h"
#include "input/emulated/ProController.h"
#include "input/emulated/WiimoteController.h"

ControllerPtr CreateDefaultDeviceController()
{
	auto controller = ControllerFactory::CreateController(InputAPI::Device, "device", "device");
	controller->set_use_motion(true);
	return controller;
}

EmulatedControllerManager::EmulatedControllerManager(size_t index) : m_index(index)
{
	m_emulatedController = InputManager::instance().get_controller(m_index);
}

EmulatedControllerManager& EmulatedControllerManager::GetController(size_t index)
{
	auto& controller = s_emulatedControllers.at(index);

	if (!controller)
	{
		controller = std::unique_ptr<EmulatedControllerManager>(new EmulatedControllerManager(index));
	}

	return *controller;
}

void EmulatedControllerManager::SetButtonValue(uint64 mappingId, bool value)
{
	if (!m_emulatedController)
	{
		return;
	}

	m_emulatedController->setButtonValue(mappingId, value);
}

void EmulatedControllerManager::SetAxisValue(uint64 mappingId, float value)
{
	if (!m_emulatedController)
	{
		return;
	}

	m_emulatedController->setAxisValue(mappingId, value);
}

void EmulatedControllerManager::SetType(EmulatedController::Type type)
{
	if (m_emulatedController && m_emulatedController->type() == type)
	{
		return;
	}

	m_emulatedController = InputManager::instance().set_controller(m_index, type);
}

void EmulatedControllerManager::SetMapping(uint64 mappingId, ControllerPtr controller, uint64 buttonId)
{
	if (!m_emulatedController || !controller)
	{
		return;
	}

	const auto& controllers = m_emulatedController->get_controllers();
	auto controllerIt = std::find_if(controllers.begin(), controllers.end(), [&](const ControllerPtr& c) { return c->api() == controller->api() && c->uuid() == controller->uuid(); });
	if (controllerIt == controllers.end())
		m_emulatedController->add_controller(controller);
	else
		controller = *controllerIt;
	m_emulatedController->set_mapping(mappingId, controller, buttonId);
}

std::optional<std::string> EmulatedControllerManager::GetMapping(uint64 mapping) const
{
	if (!m_emulatedController)
		return {};
	auto controller = m_emulatedController->get_mapping_controller(mapping);
	if (!controller)
		return {};
	auto mappingName = m_emulatedController->get_mapping_name(mapping);
	return fmt::format("{}: {}", controller->display_name(), mappingName);
}

// [first, end) of the mapping ids of a controller type
static std::pair<uint64, uint64> MappingIdRange(EmulatedController::Type type)
{
	if (type == EmulatedController::Type::VPAD)
		return {VPADController::ButtonId::kButtonId_A, VPADController::ButtonId::kButtonId_Max};
	if (type == EmulatedController::Type::Pro)
		return {ProController::ButtonId::kButtonId_A, ProController::ButtonId::kButtonId_Max};
	if (type == EmulatedController::Type::Classic)
		return {ClassicController::ButtonId::kButtonId_A, ClassicController::ButtonId::kButtonId_Max};
	if (type == EmulatedController::Type::Wiimote)
		return {WiimoteController::ButtonId::kButtonId_A, WiimoteController::ButtonId::kButtonId_Max};
	return {0, 0};
}

std::map<uint64, std::string> EmulatedControllerManager::GetMappings() const
{
	if (!m_emulatedController)
		return {};

	std::map<uint64, std::string> mappings;
	const auto [firstMapping, endMapping] = MappingIdRange(m_emulatedController->type());
	for (uint64 mapping = firstMapping; mapping < endMapping; mapping++)
	{
		if (auto mappingName = GetMapping(mapping))
		{
			mappings[mapping] = *mappingName;
		}
	}

	return mappings;
}

std::vector<std::string> EmulatedControllerManager::GetMappedAndroidControllerUuids() const
{
	std::vector<std::string> uuids;
	if (!m_emulatedController)
		return uuids;
	const auto [firstMapping, endMapping] = MappingIdRange(m_emulatedController->type());
	for (uint64 mapping = firstMapping; mapping < endMapping; mapping++)
	{
		const auto controller = m_emulatedController->get_mapping_controller(mapping);
		if (controller && controller->api() == InputAPI::Android &&
			std::find(uuids.cbegin(), uuids.cend(), controller->uuid()) == uuids.cend())
			uuids.emplace_back(controller->uuid());
	}
	return uuids;
}

std::map<uint64, uint64> EmulatedControllerManager::GetMappingButtons(std::string_view uuid) const
{
	std::map<uint64, uint64> buttons;
	if (!m_emulatedController)
		return buttons;
	const auto [firstMapping, endMapping] = MappingIdRange(m_emulatedController->type());
	for (uint64 mapping = firstMapping; mapping < endMapping; mapping++)
	{
		const auto controller = m_emulatedController->get_mapping_controller(mapping);
		if (!controller || controller->api() != InputAPI::Android || controller->uuid() != uuid)
			continue;
		if (const auto button = m_emulatedController->get_mapping_button(mapping))
			buttons[mapping] = *button;
	}
	return buttons;
}

void EmulatedControllerManager::ReplaceAndroidMappings(ControllerPtr controller, const std::map<uint64, uint64>& buttons)
{
	if (!m_emulatedController || !controller)
		return;
	// removing a controller also removes its mappings; the phone's own motion controller (InputAPI::Device) stays
	std::vector<ControllerPtr> androidControllers;
	for (const auto& existing : m_emulatedController->get_controllers())
	{
		if (existing->api() == InputAPI::Android)
			androidControllers.emplace_back(existing);
	}
	for (const auto& existing : androidControllers)
		m_emulatedController->remove_controller(existing);
	const auto [firstMapping, endMapping] = MappingIdRange(m_emulatedController->type());
	for (uint64 mapping = firstMapping; mapping < endMapping; mapping++)
		m_emulatedController->delete_mapping(mapping);
	for (const auto& [mapping, button] : buttons)
		SetMapping(mapping, controller, button);
}

void EmulatedControllerManager::SetDisabled()
{
	InputManager::instance().delete_controller(m_index, true);
	m_emulatedController.reset();
}

EmulatedControllerPtr EmulatedControllerManager::GetControllerPtr() const
{
	return m_emulatedController;
}

void EmulatedControllerManager::Reload()
{
	m_emulatedController = InputManager::instance().get_controller(m_index);
}

void EmulatedControllerManager::ClearMapping(uint64 mapping)
{
	if (!m_emulatedController)
	{
		return;
	}

	m_emulatedController->delete_mapping(mapping);
}
