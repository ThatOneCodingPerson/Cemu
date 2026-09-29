#pragma once

#include "input/InputManager.h"
#include "input/api/Controller.h"

ControllerPtr CreateDefaultDeviceController();

class EmulatedControllerManager
{
  private:
	size_t m_index;
	inline static std::array<std::unique_ptr<EmulatedControllerManager>, InputManager::kMaxController> s_emulatedControllers;
	EmulatedControllerPtr m_emulatedController;

	EmulatedControllerManager(size_t index);

  public:
	static EmulatedControllerManager& GetController(size_t index);

	void SetButtonValue(uint64 mappingId, bool value);

	void SetAxisValue(uint64 mappingId, float value);

	void SetType(EmulatedController::Type type);

	void SetMapping(uint64 mappingId, ControllerPtr controller, uint64 buttonId);

	std::optional<std::string> GetMapping(uint64 mapping) const;

	std::map<uint64, std::string> GetMappings() const;

	// Controller model profiles (the app copies mappings between devices of the same model):
	// uuids (Android input device descriptors) of the Android controllers that have a mapping
	std::vector<std::string> GetMappedAndroidControllerUuids() const;
	// mapping id -> button of the mappings to the Android controller with this uuid
	std::map<uint64, uint64> GetMappingButtons(std::string_view uuid) const;
	// replaces all mappings and Android controllers with these mappings to one controller
	void ReplaceAndroidMappings(ControllerPtr controller, const std::map<uint64, uint64>& buttons);

	void SetDisabled();

	EmulatedControllerPtr GetControllerPtr() const;

	// after InputManager replaced the controller (e.g. a profile was loaded)
	void Reload();

	void ClearMapping(uint64 mapping);
};
