# LCVMCoopLearning
 Luis and Víc personal repository
 
Ejemplo de lo que se envia en el cuerpo de un POST.

time=4%3A+55&temp=65&masterCtrl=COOL&fanCtrl=AUTO&powerOn=ON&swing=ON&air_direction=1&submit=Enviar

-> Siempre se envian por el post:
time = Tiempo actual.

&temp = temperatura. Valores posibles: de 18 a 31 grados en todos los modos. De 16 a 31 unicamente en modo HEAT.

&masterCtrl = masterControl. Modos (valores posibles): "AUTO", "COOL", "DRY", "FAN", "HEAT".

&fanCtrl= fanControl. Modos (valores posibles): "AUTO", "HIGH", "MED", "LOW", "QUIET".

-> Se envian solo si estan pulsadas (checked): 
(En realidad puedes enciar el valor que quieras, ya que solo se comprueba si se ha enviado la variable o no.)

powerOn = Botton de POWER ON. Solo se envia la variable si esta "checked"

&swing = Botton de SWING. Solo se envia la variable si esta "checked"

&air_direction botton de AIR DIRECTION. Solo se envia la variable si esta "checked"

-> Variables que siempre se envian pero que aun no les he dado uso.

&time = Tiempo actual desde que se hizo la petición al server.

&submit Se envia por el boton del formulario. Nose si en la app tambien se envia.