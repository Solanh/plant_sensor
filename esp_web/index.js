const client = mqtt.connect("ws://raspberrypi:9001", {
    reconnectPeriod: 2000,
    keepalive: 60,
    connectTimeout: 15000,
});

const STORAGE_KEY = "plants-info-v1";
let info = {};

try {
    info = JSON.parse(localStorage.getItem(STORAGE_KEY)) || {};
} catch {
    info = {};
}

$(document).ready(renderPlants);

client.on("connect", () => {
    console.log("MQTT connected");
    client.subscribe("plants/+");
});

client.on("message", (topic, payloadBuf) => {
    const payloadText = new TextDecoder().decode(payloadBuf);
    const parts = topic.split("/");

    if (parts.length !== 2 || parts[0] !== "plants") return;

    const id = parts[1];

    let data;
    try {
        data = JSON.parse(payloadText);
        console.log(data);
    } catch {
        console.log("Not valid JSON:", payloadText);
        return;
    }

    if (!data || !data.device_id) return;

    info[id] = data;
    localStorage.setItem(STORAGE_KEY, JSON.stringify(info));
    renderPlants();
});

function renderPlants() {
    const $root = $("#plants");
    const html = Object.values(info)
        .filter((p) => p && p.device_id)
        .map(loadPlants)
        .join("");
    $root.html(html);
}

// function loadPlants(plant) {
//     // guard
//     if (!plant || !plant.device_id) return "";
//     return /* html */ `
//     <div class="col-12 col-lg-4">
//       <div class="card " data-id="${plant.device_id}">
//         <img src="imgs/plant_emoji.svg" class="card-img-top mx-auto" role="presentation"/>
//         <div class="card-body">
//           <div class="btn-group mx-auto">
//             <button class="btn btn-primary">Update</button>
//             <button class="btn btn-warning">Grow Light On</button>
//           </div>
//           <div class="card-text">Soil moisture: ${plant.moisture}%</div>
//           <div class="card-text-secondary"><em>Last updated: ${plant.timestamp}</em></div>
//         </div>
//       </div>
//     </div>
//   `;
// }

function publishCmd(id, payload) {
    const topic = `plants/${id}/cmd`;
    const msg = JSON.stringify(payload);
    client.publish(topic, msg, {}, (err) => {
        if (err) console.error("Publish error:", err);
        else console.log(`Sent to ${topic}: ${msg}`);
    });
}

function loadPlants(plant) {
    if (!plant || !plant.device_id) return "";
    const id = plant.device_id;
    // default UI state (you can hydrate from info[id] later if you publish state)
    const powerChecked = plant.power ? "checked" : ""; // optional if you publish power
    const brightnessVal = plant.brightness ?? 255; // optional if you publish brightness

    return /* html */ `
  <div class="col-12 col-lg-4">
    <div class="card" data-id="${id}">
      <img src="imgs/plant_emoji.svg" class="card-img-top mx-auto" role="presentation"/>
      <div class="card-body">
        <div class="btn-group mx-auto mb-2">
          <button class="btn btn-primary js-update">Update</button>
        </div>

        <div class="d-flex align-items-center justify-content-between mb-2">
          <label class="form-check-label" for="pw-${id}"><strong>Grow Light</strong></label>
          <div class="form-check form-switch">
            <input class="form-check-input js-power" id="pw-${id}" type="checkbox" ${powerChecked}>
          </div>
        </div>

        <div class="mb-2">
          <label for="br-${id}" class="form-label">Brightness</label>
          <div class="d-flex align-items-center" style="gap:.75rem;">
            <input class="form-range js-brightness flex-grow-1" id="br-${id}" type="range" min="0" max="255" value="${brightnessVal}">
            <span class="js-bv">${brightnessVal}</span>
          </div>
        </div>

        <div class="card-text">Soil moisture: ${plant.moisture}%</div>
        <div class="card-text-secondary"><em>Last updated: ${plant.timestamp}</em></div>
      </div>
    </div>
  </div>`;
}

// $(document).on("click", ".btn-primary", function () {
//     const id = $(this).closest(".card").data("id");
//     const topic = `plants/${id}/cmd`;
//     const message = JSON.stringify({ action: "update" });
//     client.publish(topic, message);
//     console.log(`Sent to ${topic}: ${message}`);
// });

$(document).on("click", ".js-update", function () {
    const id = $(this).closest(".card").data("id");
    publishCmd(id, { action: "update" });
});

$(document).on("change", ".js-power", function () {
  const id = $(this).closest(".card").data("id");
  const on = this.checked;
  publishCmd(id, { power: on });

  info[id] = { ...(info[id] || {}), power: on };
  localStorage.setItem(STORAGE_KEY, JSON.stringify(info));
});

const brTimers = new Map();
$(document).on("input", ".js-brightness", function () {
    const $card = $(this).closest(".card");
    const id = $card.data("id");
    const val = Number(this.value);
    $card.find(".js-bv").text(val);
    clearTimeout(brTimers.get(id));
    brTimers.set(
        id,
        setTimeout(() => publishCmd(id, { brightness: val }), 120)
    );
});

client.on("reconnect", () => console.log("reconnecting…"));
client.on("close", () => console.log("disconnected"));
client.on("error", (e) => console.error(e));
