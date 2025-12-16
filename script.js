// =========================================================================
// ** CONFIGURATION **
// =========================================================================

// !!! สำคัญ: แก้ไข IP Address ของ ESP32 !!!
const ESP32_IP = '172.20.10.5'; 
const FETCH_INTERVAL = 200; // ดึงข้อมูลทุก 200 มิลลิวินาที 

// *** การตั้งค่าสำหรับคำนวณมูลค่าไฟฟ้า ***
// 1 Joule = 2.777 x 10^-7 Kilowatt-hour (kWh)
const JOULE_TO_KWH = 2.777e-7; 
// 1 kWh มีมูลค่ากี่บาท? (ใช้ค่าเฉลี่ยประมาณ 4.0 บาท/kWh)
const COST_PER_KWH_BAHT = 4.0; 

// =========================================================================
// ** ตัวแปร DOM **
// =========================================================================
const energyBarFill = document.getElementById('energy-bar-fill');
const voltageDisplay = document.getElementById('voltage-display');
const currentDisplay = document.getElementById('current-display'); 
const powerDisplay = document.getElementById('power-display'); 
const energyDisplay = document.getElementById('energy-display'); 
const potentialEnergyDisplay = document.getElementById('potential-energy-display'); // NEW
const timestampDisplay = document.getElementById('timestamp-display'); 
const statusMessage = document.getElementById('status-message');
const gameContainer = document.getElementById('game-container');

// ตัวแปรสำหรับส่วนสรุป
const energyKwhDisplay = document.getElementById('energy-kwh-display');
const savingsDisplay = document.getElementById('savings-display');

let isUltimateReady = false; 

// =========================================================================
// ** ฟังก์ชันหลัก: ดึงข้อมูลและคำนวณ **
// =========================================================================

function calculateSavings(totalJoules) {
    // 1. แปลง Joul (J) เป็น Kilowatt-hour (kWh)
    const energyKWH = totalJoules * JOULE_TO_KWH;
    
    // 2. คำนวณมูลค่า (บาท)
    const savingsBaht = energyKWH * COST_PER_KWH_BAHT;
    
    return {
        kwh: energyKWH,
        baht: savingsBaht
    };
}

function fetchEnergyData() {
    fetch(`http://${ESP32_IP}/`)
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.json();
        })
        .then(data => {
            // ดึงค่าทั้งหมดจาก JSON
            const voltage = parseFloat(data.voltage_V); 
            const totalEnergy = parseFloat(data.energy_Joules); // E = Integral Pdt (ใช้สำหรับคำนวณ Savings)
            const potentialEnergy = parseFloat(data.potentialE_Joules); // E = 1/2CV²
            const current = parseFloat(data.current_mA);
            const power = parseFloat(data.power_mW);
            const status = data.status; 
            const targetVoltage = parseFloat(data.target_V); 

            // 1. คำนวณเปอร์เซ็นต์เติมหลอดพลังงาน (ใช้ VOLTAGE V)
            let energyPercentage = (voltage / targetVoltage) * 100;
            energyPercentage = Math.min(100, Math.max(0, energyPercentage)); 

            // 2. คำนวณมูลค่าไฟฟ้าที่สร้างได้ (ใช้ Total Energy (Integrated))
            const savingsData = calculateSavings(totalEnergy);

            // 3. อัปเดต DOM
            energyBarFill.style.width = `${energyPercentage}%`;
            timestampDisplay.textContent = `Time: ${data.timestamp}`;
            voltageDisplay.textContent = `Voltage: ${voltage.toFixed(4)} V`; 
            currentDisplay.textContent = `Current: ${current.toFixed(4)} mA`;
            powerDisplay.textContent = `Power: ${power.toFixed(4)} mW`;
            energyDisplay.textContent = `Total Energy (Integrated): ${totalEnergy.toFixed(6)} J`; 
            // *** NEW: อัปเดต Potential Energy ***
            potentialEnergyDisplay.textContent = 
                `Potential Energy (½CV²): ${potentialEnergy.toFixed(6)} J`;
            
            statusMessage.textContent = `Status: ${status}`;
            
            // *** อัปเดตส่วนสรุปพลังงาน (Savings) ***
            energyKwhDisplay.textContent = `Energy Generated: ${savingsData.kwh.toFixed(9)} kWh`;
            savingsDisplay.textContent = `Estimated Savings: ${savingsData.baht.toFixed(5)} บาท`;

            // 4. Logic สำหรับ Ultimate Mode
            if (status === "ULTIMATE_READY" && !isUltimateReady) {
                handleUltimateReady();
            } else if (status !== "ULTIMATE_READY" && isUltimateReady) {
                isUltimateReady = false;
                gameContainer.classList.remove('ultimate-active');
            }
            
        })
        .catch(error => {
            // การจัดการข้อผิดพลาดในการเชื่อมต่อ
            console.error('Error fetching data:', error);
            statusMessage.textContent = "Status: Disconnected! (Check IP/ESP32)";
            // อัปเดตค่า N/A เมื่อขาดการเชื่อมต่อ
            timestampDisplay.textContent = `Time: N/A`;
            voltageDisplay.textContent = `Voltage: N/A`;
            currentDisplay.textContent = `Current: N/A`;
            powerDisplay.textContent = `Power: N/A`;
            energyDisplay.textContent = `Total Energy (Integrated): N/A`;
            potentialEnergyDisplay.textContent = `Potential Energy (½CV²): N/A`;
            energyKwhDisplay.textContent = `Energy Generated: N/A kWh`;
            savingsDisplay.textContent = `Estimated Savings: N/A บาท`;
            gameContainer.classList.remove('ultimate-active');
        });
}

function handleUltimateReady() {
    isUltimateReady = true;
    gameContainer.classList.add('ultimate-active'); 
    statusMessage.textContent = "ULTIMATE READY! (Target Voltage Reached)";
    console.log("Ultimate Activated!");
}

// =========================================================================
// ** การเริ่มต้น Web App **
// =========================================================================

fetchEnergyData(); 
setInterval(fetchEnergyData, FETCH_INTERVAL);