/*
 *  groups.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

function initGroupSetup(groups, root)
{
   // console.log(JSON.stringify(groups, undefined, 4));

   tableRoot = document.getElementById("groups");
   tableRoot.innerHTML = "";

   for (var i = 0; i < groups.length; i++) {
      var item = groups[i];
      // var html = "<td id=\"row_" + item.id + "\" data-id=\"" + item.id + "\" >" + item.id + "</td>";
      var html = '<td id="row_' + item.id + '" data-id="' + item.id + '" class="tableMultiColCell">';
      html += "  <input id=\"name_" + item.id + "\" class=\"rounded-border inputSetting\" type=\"text\" value=\"" + item.name + "\"/>";
      html += "</td>";
      html += "<td><button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"groupConfig(" + item.id + ", 'delete')\">Löschen</button></td>";

      if (tableRoot != null) {
         var elem = document.createElement("tr");
         elem.innerHTML = html;
         tableRoot.appendChild(elem);
      }
   }

   html =  "  <span>Gruppe: </span><input id=\"input_group\" class=\"rounded-border input\"/>";
   html += "  <button class=\"rounded-border button2\" onclick=\"groupConfig(0, 'add')\">+</button>";

   document.getElementById("addGroupDiv").innerHTML = html;
}

window.storeGroups = function()
{
   var jsonArray = [];
   var rootSetup = document.getElementById("groupContainer");
   var elements = rootSetup.querySelectorAll("[id^='row_']");

   // console.log("storeGroups");

   for (var i = 0; i < elements.length; i++) {
      var jsonObj = {};
      var id = $(elements[i]).data("id");
      jsonObj["id"] = id;
      jsonObj["name"] = $("#name_" + id).val();
      jsonArray[i] = jsonObj;
   }

   socket.send({ "event": "groupconfig", "object":
                 { "groups": jsonArray,
                   "action": "store" }
               });

   // show confirm

   document.getElementById("confirm").innerHTML = "<button class=\"rounded-border\" onclick=\"storeGroups()\">Speichern</button>";
   var elem = document.createElement("div");
   elem.innerHTML = "<br/><div class=\"info\"><b><center>Einstellungen gespeichert</center></b></div>";
   document.getElementById("confirm").appendChild(elem);
}

window.groupConfig = function(id, action)
{
   // console.log("groupConfig(" + action + ", " + id + ")");

   if (action == "delete") {
      if (confirm("Gruppe löschen?"))
         socket.send({ "event": "groupconfig", "object":
                       { "id": id,
                         "action": "del" }});
   }
   else if (action == "add") {
      socket.send({ "event": "groupconfig", "object":
                    { "group": $("#input_group").val(),
                      "action": "add" }
                  });
      $("#input_group").val("");
   }
}
