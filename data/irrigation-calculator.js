document.addEventListener('DOMContentLoaded', function() {

    // === GLOBAL VARIABLE TO STORE LATEST CALCULATION ===
    let lastCalculatedSchedule = null;

    // === PRESETS AND FACTORS (No changes here) ===
    const PLANT_PROFILES = {
        seedlings: { growthStage: 'seedling', substrateType: 'soil', temperature: 22, humidity: 70 },
        leafyGreens: { growthStage: 'vegetative', substrateType: 'soil', temperature: 21, humidity: 60 },
        fruitingPlants: { growthStage: 'flowering', substrateType: 'soil', temperature: 24, humidity: 55 },
        cannabisVeg: { growthStage: 'vegetative', substrateType: 'cocoCoir', temperature: 25, humidity: 60 },
        cannabisFlower: { growthStage: 'flowering', substrateType: 'cocoCoir', temperature: 23, humidity: 50 }
    };
    const SUBSTRATE_FACTORS = { 'soil': { factor: 1.0 }, 'cocoCoir': { factor: 1.2 }, 'rockwool': { factor: 1.1 }, 'perlite': { factor: 1.3 }, 'peatMoss': { factor: 1.0 }, 'hydroton': { factor: 1.4 } };
    const LIGHT_SCHEDULE = { 'seedling': 18, 'vegetative': 18, 'flowering': 12, 'lateFlowering': 12 };
    const MIN_WATERING_FREQUENCY = { 'seedling': 2, 'vegetative': 3, 'flowering': 4, 'lateFlowering': 5 };
    const MAX_WATERING_FREQUENCY = { 'seedling': 6, 'vegetative': 8, 'flowering': 12, 'lateFlowering': 15 };
    const GROWTH_STAGE_FACTORS = { 'seedling': 0.5, 'vegetative': 1, 'flowering': 1.2, 'lateFlowering': 1.5 };
    
    // === EVENT LISTENERS ===
    const calculateButton = document.getElementById('calculateButton');
    const toggleAdvancedButton = document.getElementById('toggle-advanced');
    const advancedSettingsDiv = document.getElementById('advanced-settings');
    const applyButton = document.getElementById('applyButton'); // New button

    calculateButton.addEventListener('click', calculateIrrigation);
    toggleAdvancedButton.addEventListener('click', () => {
        const isAdvanced = advancedSettingsDiv.style.display === 'block';
        advancedSettingsDiv.style.display = isAdvanced ? 'none' : 'block';
        toggleAdvancedButton.textContent = isAdvanced ? 'Show Advanced Settings' : 'Hide Advanced Settings';
    });

    // NEW: Event listener for the apply button
    applyButton.addEventListener('click', applyScheduleToDevice);

    // Validation function (No changes here)
    function validateIrrigationForm(isAdvanced) {
        document.querySelectorAll('.input-error').forEach(el => el.classList.remove('input-error'));
        const numPlants = document.getElementById('numPlants');
        const potSize = document.getElementById('potSize');
        const pumpFlowRate = document.getElementById('pumpFlowRate');
        let valid = true;
        let messages = [];
        if (!numPlants.value || Number(numPlants.value) <= 0) { valid = false; messages.push('Number of Plants is required.'); numPlants.classList.add('input-error'); }
        if (!potSize.value || Number(potSize.value) <= 0) { valid = false; messages.push('Pot Size is required.'); potSize.classList.add('input-error'); }
        if (!pumpFlowRate.value || Number(pumpFlowRate.value) <= 0) { valid = false; messages.push('Pump Flow Rate is required.'); pumpFlowRate.classList.add('input-error'); }
        if (isAdvanced) { /* ... additional validation ... */ }
        if (!valid) { alert(messages.join('\n')); }
        return valid;
    }

    // === MAIN CALCULATION FUNCTION (No major changes, just stores the result) ===
    function calculateIrrigation() {
        const isAdvanced = advancedSettingsDiv.style.display === 'block';
        if (!validateIrrigationForm(isAdvanced)) return;

        // ... (All your existing calculation logic remains the same) ...
        const numPlants = parseInt(document.getElementById('numPlants').value);
        const potSize = parseFloat(document.getElementById('potSize').value);
        const pumpFlowRate = parseFloat(document.getElementById('pumpFlowRate').value);
        const baseWaterPercentage = parseFloat(document.getElementById('baseWaterPercentage').value) / 100;
        let growthStage, substrateType, temperature, humidity, canopySize;
        if (isAdvanced) { /* get advanced values */ } else {
            const profile = PLANT_PROFILES[document.getElementById('plantProfile').value];
            growthStage = profile.growthStage;
            temperature = profile.temperature;
            humidity = profile.humidity;
        }
        const activeHours = LIGHT_SCHEDULE[growthStage] || 18;
        let environmentalFactor = Math.min(Math.max((temperature / 25) * (1 + (100 - humidity) / 200), 0.5), 2);
        let minFrequency = MIN_WATERING_FREQUENCY[growthStage] || 2;
        let maxFrequency = MAX_WATERING_FREQUENCY[growthStage] || 6;
        let frequency = Math.round(minFrequency + (maxFrequency - minFrequency) * (environmentalFactor - 0.5) / 1.5);
        let baseWaterNeed = potSize * baseWaterPercentage * (GROWTH_STAGE_FACTORS[growthStage] || 1);
        const totalDailyWaterNeed = baseWaterNeed * numPlants;
        const waterPerSession = totalDailyWaterNeed / frequency;
        const durationPerSession = Math.round((waterPerSession / pumpFlowRate) * 60);
        const totalActiveMinutes = activeHours * 60;
        const totalWateringSeconds = durationPerSession * frequency;
        const delayBetweenActivations = (frequency > 1) ? Math.round(((totalActiveMinutes - (totalWateringSeconds / 60)) / (frequency - 1))) : 0;

        // --- Display Results (No changes here) ---
        const resultsElement = document.getElementById('results-section');
        resultsElement.style.display = 'block';
        document.getElementById('totalWaterResult').textContent = totalDailyWaterNeed.toFixed(2);
        document.getElementById('frequencyResult').textContent = frequency;
        document.getElementById('activeHoursResult').textContent = activeHours;
        document.getElementById('durationResult').textContent = durationPerSession;
        const hours = Math.floor(delayBetweenActivations / 60);
        const minutes = delayBetweenActivations % 60;
        document.getElementById('delayResult').textContent = `${hours} hours and ${minutes} minutes`;

        // --- NEW: Store the calculated values for sending to the device ---
        lastCalculatedSchedule = {
            startTime: document.getElementById('startTime').value, // Get value from new input
            frequency: frequency,
            durationPerSession: durationPerSession,
            delayBetweenActivations: delayBetweenActivations // Send delay in minutes
        };

        resultsElement.scrollIntoView({ behavior: 'smooth' });
    }

    // === NEW FUNCTION TO SEND DATA TO ESP32 ===
    async function applyScheduleToDevice() {
        if (!lastCalculatedSchedule) {
            alert("Please calculate a watering plan first.");
            return;
        }

        const statusMessage = document.getElementById('statusMessage');
        statusMessage.textContent = 'Applying schedule to device...';
        statusMessage.style.color = '#007bff';

        try {
            const response = await fetch('/calculate-schedule', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(lastCalculatedSchedule)
            });

            const result = await response.json();

            if (response.ok && result.status === 'success') {
                statusMessage.textContent = 'Success! Schedule has been applied.';
                statusMessage.style.color = '#28a745'; // Green
            } else {
                throw new Error(result.message || 'Failed to apply schedule.');
            }
        } catch (error) {
            statusMessage.textContent = `Error: ${error.message}`;
            statusMessage.style.color = '#e74c3c'; // Red
            console.error('Failed to send schedule:', error);
        }
    }
}());
