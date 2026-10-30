#include "OptionsHelp.h"
#include "AppDialogs.h"
#include "PictureViewer.h"
#include "UiStyle.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <map>

namespace ui::help {

    namespace {

        // ============================================================================
        // Content
        // ============================================================================

        struct Block {
            enum class Kind { Heading, Text, Picture, Pair, Triple, Note } kind;
            QString text;               // heading, text or note (rich text)
            QString first, first_caption;
            QString second, second_caption;
            QString third, third_caption; // Triple only
        };

        struct Topic {
            QString id;
            QString group;
            QString title;
            style::Icon icon;
            QString summary;
            std::vector<Block> blocks;
            QString keywords; // extra words the search finds it by
        };

        Block heading(const QString& t) { return { Block::Kind::Heading, t, {}, {}, {}, {}, {}, {} }; }
        Block text(const QString& t) { return { Block::Kind::Text, t, {}, {}, {}, {}, {}, {} }; }
        Block note(const QString& t) { return { Block::Kind::Note, t, {}, {}, {}, {}, {}, {} }; }
        Block picture(const QString& p, const QString& caption) { return { Block::Kind::Picture, {}, p, caption, {}, {}, {}, {} }; }
        Block pair(const QString& a, const QString& ca, const QString& b, const QString& cb) {
            return { Block::Kind::Pair, {}, a, ca, b, cb, {}, {} };
        }
        // Three steps in one row: typically the menu, the selection and the result.
        Block triple(const QString& a, const QString& ca, const QString& b, const QString& cb,
                     const QString& c, const QString& cc) {
            return { Block::Kind::Triple, {}, a, ca, b, cb, c, cc };
        }
        QString pic(const char* topic, int n) {
            return QStringLiteral(":/information/%1/%2.png").arg(QLatin1String(topic)).arg(n, 2, 10, QLatin1Char('0'));
        }

        const QString BACKGROUND = QStringLiteral("Fondo del esquema");
        const QString BOX_MENU = QStringLiteral("Menú de la caja");
        const QString CONNECTION_MENU = QStringLiteral("Menú de la conexión");
        const QString EQUIVALENCES = QStringLiteral("Equivalencias");
        const QString JOINT = QStringLiteral("Esquema conjunto");
        const QString TABS = QStringLiteral("Pestañas");
        const QString PROJECT = QStringLiteral("Proyecto");

        const QString HOW = QStringLiteral("Cómo se usa");
        const QString EXAMPLE = QStringLiteral("Ejemplo");

        std::vector<Topic> buildTopics() {
            using I = style::Icon;
            std::vector<Topic> t;

            // ── Background ──────────────────────────────────────────────────
            t.push_back({ "newBox", BACKGROUND, QStringLiteral("Nueva caja"), I::NewBox,
                QStringLiteral("Crea una caja suelta, sin conexiones, justo donde haces clic derecho."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho en una zona vacía del lienzo y elige <b>Nueva caja</b>. "
                                        "En el diálogo escribe su nombre, elige su aspecto y pulsa <b>Crear</b>.")),
                    heading(QStringLiteral("Importa dónde haces clic")),
                    text(QStringLiteral("La caja aparece en el punto del clic:"
                                        "<ul style=\"margin-left:-18px\">"
                                        "<li><b>A la altura de un nivel</b>, entra en ese nivel.</li>"
                                        "<li><b>Entre dos cajas</b> de un nivel, se coloca entre ellas, respetando el "
                                        "orden de izquierda a derecha.</li>"
                                        "<li><b>Por encima del primer nivel o por debajo del último</b>, abre un nivel "
                                        "nuevo solo para ella.</li></ul>")),
                    pair(pic("newBox", 1), QStringLiteral("Clic derecho entre las cajas 2 y 3 (marcado en morado)…"),
                         pic("newBox", 2), QStringLiteral("…y la caja 4 queda entre ellas, en su mismo nivel.")),
                    pair(pic("newBox", 3), QStringLiteral("Clic derecho por encima del primer nivel…"),
                         pic("newBox", 4), QStringLiteral("…y la caja 5 abre un nivel nuevo arriba.")),
                    note(QStringLiteral("En el esquema conjunto no se pueden crear cajas: allí se "
                                        "reúnen las de los demás esquemas.")),
                },
                QStringLiteral("crear añadir fondo clic posición nivel suelta") });

            t.push_back({ "paste", BACKGROUND, QStringLiteral("Pegar"), I::Paste,
                QStringLiteral("Pega como un bloque nuevo lo que copiaste, justo donde haces clic derecho."),
                {
                    heading(HOW),
                    text(QStringLiteral("Primero copia un bloque o un esquema (ver <b>Copiar bloque o esquema</b>). "
                                        "Después haz clic derecho en una zona vacía del esquema donde lo quieras: "
                                        "<b>Pegar</b> aparece justo debajo de <b>Nueva caja</b>. Lo pegado es una copia "
                                        "independiente, con las mismas cajas, nombres, colores y conexiones.")),
                    pair(pic("paste", 1), QStringLiteral("Clic derecho en el fondo, a la derecha › Pegar."),
                         pic("paste", 2), QStringLiteral("La copia del bloque 1–2 aparece en ese sitio.")),
                    heading(QStringLiteral("Importa dónde haces clic")),
                    text(QStringLiteral("Lo pegado se coloca como si soltaras un bloque que estás moviendo:"
                                        "<ul style=\"margin-left:-18px\">"
                                        "<li>Su primera fila de cajas entra en el <b>nivel del clic</b>; por encima del "
                                        "primer nivel o por debajo del último, se abren niveles nuevos.</li>"
                                        "<li>Queda <b>entre los bloques</b> que hay a izquierda y derecha del clic. Si "
                                        "haces clic encima de un bloque, va a su izquierda o a su derecha, según la "
                                        "mitad en la que caiga el clic.</li></ul>")),
                    heading(QStringLiteral("Atajo y otras ventanas")),
                    text(QStringLiteral("<b>Ctrl+V</b> pega donde está el ratón (o en el centro de la vista). Lo copiado "
                                        "se guarda en el portapapeles del sistema, así que también se puede pegar en "
                                        "otra ventana abierta de la aplicación, aunque se haya cerrado la ventana de "
                                        "la que salió. Se puede pegar tantas veces como quieras.")),
                    note(QStringLiteral("<b>Pegar</b> solo aparece cuando hay algo que pegar: si después copias otra cosa "
                                        "(por ejemplo, un texto), desaparece. En el esquema conjunto no se puede pegar. "
                                        "Con <b>Ctrl+Z</b> se deshace.")),
                },
                QStringLiteral("pegar portapapeles ctrl+v copia duplicar ventana") });

            // ── Box menu ────────────────────────────────────────────────────
            t.push_back({ "createAbove", BOX_MENU, QStringLiteral("Crear caja arriba"), I::BoxAbove,
                QStringLiteral("Crea una caja nueva por encima de esta y la une a ella."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la caja y elige <b>Crear caja arriba</b>. La caja nueva "
                                        "se coloca en el nivel inmediatamente superior y queda conectada por encima de "
                                        "la que elegiste, que conserva el resto de sus conexiones.")),
                    pair(pic("createAbove", 1), QStringLiteral("Clic derecho en la caja 3 › Crear caja arriba."),
                         pic("createAbove", 2), QStringLiteral("La caja 4 queda encima de la 3, que sigue unida a la 1.")),
                    note(QStringLiteral("Si la caja está en el primer nivel, baja un nivel para dejar sitio a la nueva.")),
                },
                QStringLiteral("padre superior nueva") });

            t.push_back({ "createBelow", BOX_MENU, QStringLiteral("Crear caja debajo"), I::BoxBelow,
                QStringLiteral("Crea una caja nueva por debajo de esta y la une a ella."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la caja y elige <b>Crear caja debajo</b>. La caja nueva "
                                        "se coloca en el nivel siguiente (si no existe, se crea) y queda conectada por "
                                        "debajo de la que elegiste.")),
                    pair(pic("createBelow", 1), QStringLiteral("Clic derecho en la caja 2 › Crear caja debajo."),
                         pic("createBelow", 2), QStringLiteral("La caja 4 queda debajo de la 2, en un nivel nuevo.")),
                },
                QStringLiteral("hijo inferior nueva") });

            t.push_back({ "createSide", BOX_MENU, QStringLiteral("Crear caja a la izquierda o a la derecha"), I::BoxRight,
                QStringLiteral("Crea una caja suelta en el mismo nivel, pegada a esta."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la caja y elige <b>Crear caja a la izquierda</b> o "
                                        "<b>a la derecha</b>. La caja nueva aparece justo a ese lado, en el mismo nivel "
                                        "y sin ninguna conexión.")),
                    pair(pic("createSide", 1), QStringLiteral("Clic derecho en la caja 2 › Crear caja a la derecha."),
                         pic("createSide", 2), QStringLiteral("La caja 4 queda junto a la 2, sin conexiones.")),
                },
                QStringLiteral("lado izquierda derecha mismo nivel suelta hermana") });

            t.push_back({ "addChildConnection", BOX_MENU, QStringLiteral("Caja «actual» por debajo de"), I::CurrentBelow,
                QStringLiteral("Conecta esta caja por debajo de otra que eliges a continuación."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la caja y elige <b>Caja «…» por debajo de</b> (el menú "
                                        "muestra el nombre de la caja). Después haz clic en la caja que debe quedar "
                                        "<b>por encima</b>: las que se pueden elegir se resaltan y el resto se apaga. "
                                        "Pulsa <b>Esc</b> para cancelar.")),
                    text(QStringLiteral("Si hace falta, la caja baja de nivel, con todo lo que tiene debajo, para quedar "
                                        "por debajo de la elegida. No se ofrecen las cajas que crearían un ciclo ni las "
                                        "que ya están por encima directa o indirectamente.")),
                    pair(pic("addChildConnection", 1), QStringLiteral("Clic derecho en la caja 2 › Caja «2» por debajo de; después, clic en la caja 1."),
                         pic("addChildConnection", 2), QStringLiteral("La caja 2 está por debajo de la caja 1.")),                          
                    heading(QStringLiteral("Reagrupar conexiones")),
                    text(QStringLiteral("La opción también sirve para <b>reagrupar</b> las conexiones. Si las dos cajas "
                                        "ya están unidas a través de una conexión compartida con otras, elegirlas les da "
                                        "una <b>conexión exclusiva</b>; el resto de la conexión sigue como estaba.")),
                    text(QStringLiteral("Por ejemplo, aquí una sola conexión une la caja 1 con la 2 y la 3. Para que la 1 "
                                        "y la 2 tengan una conexión exclusiva entre ellas, haz clic derecho en la caja 2, "
                                        "elige <b>Caja «2» por debajo de</b> y selecciona la caja 1:")),
                    picture(pic("addChildConnection", 3), QStringLiteral("Una sola conexión une la 1 con la 2 y la 3.")),
                    pair(pic("addChildConnection", 4), QStringLiteral("Caja «2» por debajo de: se elige la caja 1."),
                         pic("addChildConnection", 5), QStringLiteral("La 1 y la 2 tienen ahora su propia conexión.")),
                    text(QStringLiteral("Funciona igual con conexiones más complejas. Aquí una sola conexión une las cajas "
                                        "1, 2 y 3 con la 4 y la 5. Para que la 2 y la 4 tengan una conexión exclusiva, "
                                        "haz clic derecho en la caja 4, elige <b>Caja «4» por debajo de</b> y selecciona "
                                        "la caja 2:")),
                    pair(pic("addChildConnection", 6), QStringLiteral("Antes: una conexión une 1, 2 y 3 con 4 y 5."),
                         pic("addChildConnection", 7), QStringLiteral("Después: la 2 y la 4 tienen su propia conexión.")),
                },
                QStringLiteral("conectar unir debajo hijo reagrupar exclusiva separar") });

            t.push_back({ "addParentConnection", BOX_MENU, QStringLiteral("Caja «actual» por encima de"), I::CurrentAbove,
                QStringLiteral("Conecta esta caja por encima de otra que eliges a continuación."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la caja y elige <b>Caja «…» por encima de</b>. Después haz "
                                        "clic en la caja que debe quedar <b>por debajo</b>: las que se pueden elegir se "
                                        "resaltan. Pulsa <b>Esc</b> para cancelar.")),
                    text(QStringLiteral("Si hace falta, la caja elegida baja de nivel, con todo lo que tiene debajo. No se "
                                        "ofrecen las cajas que crearían un ciclo ni las que ya están por debajo directa o indirectamente"
                                        "de otras.")),
                    pair(pic("addParentConnection", 1), QStringLiteral("Clic derecho en la caja 2 › Caja «2» por encima de; después, clic en la caja 1."),
                         pic("addParentConnection", 2), QStringLiteral("La caja 2 está por encima de la caja 1.")),
                    heading(QStringLiteral("Reagrupar conexiones")),
                    text(QStringLiteral("También sirve para <b>reagrupar</b> las conexiones: si las dos cajas ya están "
                                        "unidas a través de una conexión compartida, pasan a tener una <b>conexión "
                                        "exclusiva</b> y el resto sigue como estaba.")),
                    text(QStringLiteral("Por ejemplo, para que la 1 y la 2 tengan una conexión exclusiva, haz clic derecho "
                                        "en la caja 1, elige <b>Caja «1» por encima de</b> y selecciona la caja 2:")),
                    picture(pic("addParentConnection", 3), QStringLiteral("Una sola conexión une la 1 con la 2 y la 3.")),
                    pair(pic("addParentConnection", 4), QStringLiteral("Caja «1» por encima de: se elige la caja 2."),
                         pic("addParentConnection", 5), QStringLiteral("La 1 y la 2 tienen ahora su propia conexión.")),
                    text(QStringLiteral("Con conexiones más complejas funciona igual. Para que la 2 y la 4 tengan una "
                                        "conexión exclusiva, haz clic derecho en la caja 2, elige <b>Caja «2» por "
                                        "encima de</b> y selecciona la caja 4:")),
                    pair(pic("addParentConnection", 6), QStringLiteral("Antes: una conexión une 1, 2 y 3 con 4 y 5."),
                         pic("addParentConnection", 7), QStringLiteral("Después: la 2 y la 4 tienen su propia conexión.")),
                },
                QStringLiteral("conectar unir encima padre reagrupar exclusiva separar") });

            t.push_back({ "fuse", BOX_MENU, QStringLiteral("Fusionar"), I::Fuse,
                QStringLiteral("Une dos cajas en una sola, que se queda con las conexiones de ambas."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho en una caja, elige <b>Fusionar</b> y haz clic en la otra. Se abre "
                                        "un diálogo para decidir el nombre y el aspecto de la caja resultante: puedes "
                                        "tomar cada dato de una u otra, o escribirlo.")),
                    text(QStringLiteral("Útil cuando descubres que dos cajas representan lo mismo: todo lo que estaba por "
                                        "encima o por debajo de cualquiera de ellas queda unido a la caja fusionada.")),
                    triple(pic("fuse", 1), QStringLiteral("Clic derecho en la caja 2 › Fusionar."),
                           pic("fuse", 2), QStringLiteral("Se elige la 3."),
                           pic("fuse", 3), QStringLiteral("Una sola caja, 2, une ahora la 1 con la 4.")),
                    note(QStringLiteral("No se pueden fusionar dos cajas que ya están una por encima de la otra, aunque "
                                        "sea indirectamente: se crearía un ciclo.")),
                },
                QStringLiteral("unir juntar combinar misma") });

            t.push_back({ "moveBlock", BOX_MENU, QStringLiteral("Mover bloque"), I::Move,
                QStringLiteral("Lleva una caja y todas las que están unidas a ella a otro sitio del esquema."),
                {
                    heading(HOW),
                    text(QStringLiteral("Un <b>bloque</b> es un grupo de cajas conectadas entre sí. Elige <b>Mover "
                                        "bloque</b> y el bloque quedará colgando del ratón: una silueta discontinua "
                                        "muestra dónde caerá y una etiqueta indica en qué nivel empezará. Haz clic para "
                                        "soltarlo; con clic derecho o <b>Esc</b> vuelve a su sitio.")),
                    pair(pic("moveBlock", 2), QStringLiteral("El bloque 3–4 cuelga del ratón."),
                         pic("moveBlock", 3), QStringLiteral("Soltado a la izquierda del bloque 1–2.")),
                    note(QStringLiteral("Esta opción solo aparece cuando hay más de un bloque. Si se deposita más allá del primer "
                                        "o del último nivel, se abren niveles nuevos.")),
                },
                QStringLiteral("mover desplazar grupo esquema") });

            t.push_back({ "resizeBox", BOX_MENU, QStringLiteral("Cambiar el tamaño de una caja"), I::Resize,
                QStringLiteral("Haz una caja más ancha, más estrecha, más alta o más baja arrastrando sus bordes."),
                {
                    heading(HOW),
                    text(QStringLiteral("Pasa el ratón por encima de una caja: aparece su marco, con un tirador en cada "
                                        "esquina y en el centro de cada lado. Arrastra uno de ellos. Los dos lados se "
                                        "mueven a la vez, así que la caja crece o encoge sin moverse de su sitio, y una "
                                        "etiqueta encima de ella indica su tamaño en píxeles. Al soltar, el resto del "
                                        "esquema se recoloca para dejarle sitio.")),
                    pair(pic("resizeBox", 1), QStringLiteral("Arrastrando el borde derecho de la caja 2…"),
                         pic("resizeBox", 2), QStringLiteral("…que se ensancha: la 3 se aparta para dejarle sitio.")),
                    heading(QStringLiteral("Desde las propiedades")),
                    text(QStringLiteral("En el diálogo de la caja (doble clic sobre ella, o <b>Propiedades</b> en su menú) "
                                        "el tamaño se cambia sobre la vista previa: arrastra sus bordes igual que en el "
                                        "esquema o escribe el ancho y el alto en la esquina inferior derecha. Detrás de la "
                                        "caja queda, como una sombra, el tamaño por defecto de su forma, para compararlos.")),
                    picture(pic("resizeBox", 3), QStringLiteral("Una caja más ancha que su forma, con la sombra del tamaño "
                                                                "por defecto detrás.")),
                    heading(QStringLiteral("Volver al tamaño de la forma")),
                    text(QStringLiteral("En la vista previa, pulsa <b>↺</b> junto a las medidas. Mientras una caja tiene el "
                                        "tamaño de su forma, lo sigue: si le cambias la forma, cambia con ella. Si solo "
                                        "arrastras un lado (el ancho o el alto), el otro sigue siendo el de la forma; en "
                                        "la vista previa se ve en gris.")),
                    note(QStringLiteral("El ancho va de 40 a 400 píxeles y el alto de 30 a 300. La caja no crece sola con "
                                        "el texto: si no cabe, se desplaza con la rueda del ratón. Al fusionar dos cajas "
                                        "puedes quedarte con el tamaño de cualquiera de ellas. Con <b>Ctrl+Z</b> se deshace.")),
                },
                QStringLiteral("tamaño redimensionar ancho alto agrandar ampliar estrechar encoger bordes tiradores píxeles") });

            t.push_back({ "copyPiece", BOX_MENU, QStringLiteral("Copiar bloque o esquema"), I::Copy,
                QStringLiteral("Copia el bloque de esta caja, o el esquema entero, para pegarlo en cualquier esquema."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre una caja (o una conexión) y elige en la sección "
                                        "<b>Copiar</b>:"
                                        "<ul style=\"margin-left:-18px\">"
                                        "<li><b>Copiar bloque</b>: la caja y todas las que están unidas a ella, con sus "
                                        "conexiones. Solo aparece cuando el esquema tiene más de un bloque.</li>"
                                        "<li><b>Copiar esquema</b>: todos los bloques del esquema, colocados como "
                                        "están.</li></ul>")),
                    picture(pic("copyPiece", 1),
                            QStringLiteral("Clic derecho en la caja 3 › Copiar bloque (solo el 3–4) o Copiar esquema "
                                           "(también el 1–2).")),
                    text(QStringLiteral("Luego pégalo con <b>Pegar</b>, en el menú del fondo de cualquier esquema, o con "
                                        "<b>Ctrl+V</b>. Se copian los nombres, el aspecto de las cajas y el estilo de "
                                        "las líneas, también las conexiones hipotéticas.")),
                    heading(QStringLiteral("Atajo")),
                    text(QStringLiteral("<b>Ctrl+C</b> copia el bloque de la caja o conexión que está bajo el ratón; "
                                        "con el ratón sobre el fondo, copia el esquema entero.")),
                    note(QStringLiteral("En el esquema conjunto también se puede copiar: <b>Copiar esquema «…»</b> copia "
                                        "el esquema de la caja tal como está en el conjunto, mientras no esté mezclado con "
                                        "otros. Lo copiado se puede pegar en cualquier esquema, pero no en el "
                                        "conjunto.")),
                },
                QStringLiteral("copiar portapapeles ctrl+c bloque esquema duplicar") });
            t.push_back({ "uncertain", BOX_MENU, QStringLiteral("Conexiones hipotéticas"), I::LineDashed,
                QStringLiteral("Marca como dudosa una conexión de esta caja: se dibuja discontinua desde ella."),
                {
                    heading(HOW),
                    text(QStringLiteral("El submenú lista cada conexión de la caja, nombrada por las cajas del otro "
                                        "extremo y por si están arriba o abajo. Marca las que no tengas claras; elígelas "
                                        "de nuevo para quitar la marca.")),
                    text(QStringLiteral("Si la caja tiene cajas equivalentes al lado, el submenú lista también esas "
                                        "líneas, como <b>Con «…» (equivalencia)</b>: marcarlas es lo mismo que dibujarlas "
                                        "discontinuas desde la propia línea (ver <b>Línea de equivalencia continua o "
                                        "discontinua</b>).")),
                    pair(pic("uncertain", 1), QStringLiteral("Clic derecho en la 2 › Conexiones hipotéticas › Con «1»."),
                         pic("uncertain", 2), QStringLiteral("El tramo de la 2 se dibuja discontinuo.")),
                    note(QStringLiteral("Para marcar dudosa una conexión entera, usa <b>Usar línea discontinua</b> en el "
                                        "menú de la conexión.")),
                },
                QStringLiteral("dudosa hipotética discontinua incierta marca") });

            t.push_back({ "removePartial", BOX_MENU, QStringLiteral("Eliminar conexión parcial"), I::RemovePartial,
                QStringLiteral("Deshace la relación entre esta caja y otra unida directamente a ella."),
                {
                    heading(HOW),
                    text(QStringLiteral("Elige la opción y haz clic en la otra caja: solo se ofrecen las unidas "
                                        "directamente. Si la conexión unía más cajas, las demás siguen unidas.")),
                    triple(pic("removePartial", 1), QStringLiteral("La 1 y la 2 llegan a la 3. Clic derecho en la 2 › "
                                                                   "Eliminar conexión parcial."),
                           pic("removePartial", 2), QStringLiteral("Se elige la 3."),
                           pic("removePartial", 3), QStringLiteral("La 2 ya no está unida a la 3; la 1 sí.")),
                    note(QStringLiteral("Una caja que pierde lo que tenía encima puede subir de nivel.")),
                },
                QStringLiteral("quitar borrar romper relación") });

            t.push_back({ "removeBox", BOX_MENU, QStringLiteral("Eliminar caja"), I::RemoveBox,
                QStringLiteral("Borra la caja. Si estaba entre otras, las de arriba quedan unidas a las de abajo."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la caja y elige <b>Eliminar caja</b>. Si tenía cajas por "
                                        "encima y por debajo, la relación entre ellas no se pierde: quedan unidas "
                                        "directamente.")),
                    pair(pic("removeBox", 1), QStringLiteral("Clic derecho en la caja 2 › Eliminar caja."),
                         pic("removeBox", 2), QStringLiteral("La 1 y la 3 siguen unidas.")),
                },
                QStringLiteral("borrar quitar suprimir") });

            t.push_back({ "removeBlock", BOX_MENU, QStringLiteral("Eliminar bloque"), I::RemoveBox,
                QStringLiteral("Borra una caja junto con todas las que están unidas a ella."),
                {
                    heading(HOW),
                    text(QStringLiteral("Elige <b>Eliminar bloque</b> y confirma: desaparecen la caja, todas las cajas "
                                        "conectadas con ella (directamente o a través de otras) y sus conexiones. Solo "
                                        "aparece cuando el bloque tiene más de una caja.")),
                    pair(pic("removeBlock", 1), QStringLiteral("Clic derecho en la caja 3 › Eliminar bloque."),
                         pic("removeBlock", 2), QStringLiteral("El bloque 3–4 desaparece; el 1–2 sigue.")),
                    note(QStringLiteral("En el esquema conjunto la opción se llama <b>Quitar bloque</b>: saca esas cajas "
                                        "del esquema conjunto, sin afectar al esquema original del que provienen. Si el "
                                        "bloque es el esquema entero, aparece en su lugar <b>Quitar esquema</b>.")),
                },
                QStringLiteral("borrar quitar grupo") });

            // ── Equivalences ────────────────────────────────────────────────
            t.push_back({ "addEquivalence", EQUIVALENCES, QStringLiteral("Hacer equivalente a…"), I::Equivalent,
                QStringLiteral("Marca dos cajas como equivalentes: quedan en el mismo nivel, una junto a la otra, "
                               "unidas por una línea horizontal."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la caja, elige <b>Hacer equivalente a…</b> y haz clic en "
                                        "la otra: las que se pueden elegir se resaltan y el resto se apaga. Pulsa "
                                        "<b>Esc</b> para cancelar. La línea que las une empieza continua: la "
                                        "equivalencia se da por segura (ver <b>Línea de equivalencia continua o "
                                        "discontinua</b>).")),
                    triple(pic("addEquivalence", 1), QStringLiteral("Clic derecho en la caja 5 › Hacer equivalente a…"),
                           pic("addEquivalence", 2), QStringLiteral("Se elige la 3."),
                           pic("addEquivalence", 3), QStringLiteral("La 3 baja al nivel de la 5 y quedan unidas.")),
                    heading(QStringLiteral("Dónde quedan las cajas")),
                    text(QStringLiteral("Dos cajas equivalentes comparten nivel:"
                                        "<ul style=\"margin-left:-18px\">"
                                        "<li>Si están en niveles distintos, la de más arriba <b>baja</b> al nivel de la "
                                        "otra, con todo lo que tiene debajo.</li>"
                                        "<li>Entre dos cajas equivalentes no puede quedar ninguna otra caja, solo "
                                        "conexiones que pasan. Si había cajas entre ellas, se apartan, sin cambiar su "
                                        "orden.</li></ul>")),
                    heading(QStringLiteral("Cadenas")),
                    text(QStringLiteral("Una caja puede tener una caja equivalente a cada lado, así que se forman "
                                        "<b>cadenas</b>: si 1 es equivalente a 2 y 2 a 3, las tres lo son entre sí. Solo "
                                        "las cajas de los <b>extremos</b> de una cadena pueden hacerse equivalentes a "
                                        "otra: el lado por el que se unirían tiene que estar libre en las dos.")),
                    note(QStringLiteral("No se ofrecen las cajas que están una por encima de la otra, directa o "
                                        "indirectamente, ni las que, a través de otras cadenas, harían que algo quedara "
                                        "por encima de sí mismo. En el <b>esquema conjunto</b> se pueden hacer "
                                        "equivalentes cajas de esquemas distintos: esos esquemas quedan mezclados y ya no "
                                        "se pueden mover como un todo (no aparece <b>Mover esquema</b>), pero sí quitar "
                                        "del conjunto; al quitar uno, solo se pierden sus equivalencias con los demás.")),
                },
                QStringLiteral("equivalente equivalencia igual misma cadena línea horizontal nivel") });

            t.push_back({ "equivalenceStyle", EQUIVALENCES, QStringLiteral("Línea de equivalencia continua o discontinua"),
                I::EquivalenceDashed,
                QStringLiteral("Marca una equivalencia como segura (línea continua) o como hipotética (discontinua)."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea que une las dos cajas y elige <b>Usar línea "
                                        "discontinua</b>; para volver, <b>Usar línea continua</b>. También desde una de "
                                        "las cajas: clic derecho › <b>Conexiones hipotéticas</b> › <b>Con «…» "
                                        "(equivalencia)</b>.")),
                    pair(pic("equivalenceStyle", 1), QStringLiteral("Clic en la línea entre la 3 y la 5 › Usar línea discontinua."),
                         pic("equivalenceStyle", 2), QStringLiteral("La equivalencia queda como hipotética.")),
                    heading(QStringLiteral("En una cadena")),
                    text(QStringLiteral("Una línea continua dice que sus dos cajas son equivalentes con seguridad. La "
                                        "seguridad se transmite: si la 1 lo es con la 2 y la 2 con la 3, la 1 también lo "
                                        "es con la 3. Por eso cambiar una línea puede afectar a otras cajas de la "
                                        "cadena:"
                                        "<ul style=\"margin-left:-18px\">"
                                        "<li>Al hacerla <b>discontinua</b>, la cadena se divide por ahí: las cajas de un "
                                        "lado dejan de ser seguras con las del otro. Solo cambia esa línea.</li>"
                                        "<li>Al hacerla <b>continua</b>, todas las cajas seguras con una de las dos pasan "
                                        "a serlo con la otra, así que otras líneas pueden volverse continuas a la vez. "
                                        "Por ejemplo, en 1 – 2 – 3, si la 1 y la 3 son seguras entre sí y la 2 no lo es "
                                        "con ninguna, hacer continua la línea 2 – 3 hace continua también la 1 – 2.</li>"
                                        "</ul>")),
                    note(QStringLiteral("Cada caja recuerda con qué cajas es segura, no el sitio que ocupa: si las "
                                        "cambias de orden dentro de la cadena, puede cambiar qué líneas se dibujan "
                                        "discontinuas.")),
                },
                QStringLiteral("equivalencia hipotética dudosa segura discontinua continua estilo cadena") });

            t.push_back({ "removeEquivalence", EQUIVALENCES, QStringLiteral("Quitar equivalencia"), I::RemoveEquivalence,
                QStringLiteral("Borra la línea entre dos cajas equivalentes: la cadena se parte en dos por ahí."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre una de las cajas y elige <b>Quitar equivalencia con "
                                        "«…»</b> (hay una opción por cada lado que tenga una equivalente), o haz clic "
                                        "sobre la línea y elige <b>Quitar equivalencia</b>.")),
                    pair(pic("removeEquivalence", 1), QStringLiteral("Clic derecho en la caja 3 › Quitar equivalencia con «5»."),
                         pic("removeEquivalence", 2), QStringLiteral("La 3 vuelve a su nivel, debajo de la 1.")),
                    text(QStringLiteral("Cada parte de la cadena vuelve al nivel que le corresponde: una caja que había "
                                        "bajado para estar junto a su equivalente puede volver a subir. Las cajas de cada "
                                        "parte siguen siendo seguras con las mismas que antes.")),
                    note(QStringLiteral("<b>Eliminar caja</b> sobre una caja de una cadena no la parte: sus vecinas "
                                        "quedan unidas entre sí. <b>Fusionar</b> dos cajas junta también sus cadenas, y "
                                        "lo que era seguro con cualquiera de ellas lo es con la caja fusionada.")),
                },
                QStringLiteral("quitar borrar equivalencia cadena separar partir") });

            t.push_back({ "moveEquivalent", EQUIVALENCES, QStringLiteral("Mover cajas equivalentes"), I::Move,
                QStringLiteral("Arrastra una caja de una cadena para cambiarla de sitio en ella o llevarte la cadena entera."),
                {
                    heading(HOW),
                    text(QStringLiteral("Arrastra la caja dentro de su nivel:"
                                        "<ul style=\"margin-left:-18px\">"
                                        "<li>Mientras no pase por encima de ninguna caja que no sea de la cadena, solo ella "
                                        "cambia de sitio: puede ponerse delante o detrás de sus equivalentes, y a "
                                        "cualquier lado de las conexiones que pasan entre ellas o junto a la cadena. Las "
                                        "demás cajas de la cadena no se mueven.</li>"
                                        "<li>Si la llevas más allá de una caja que no es de la cadena, la sigue <b>toda la "
                                        "cadena</b>, y una etiqueta lo indica: «Toda la cadena se mueve».</li></ul>")),
                    text(QStringLiteral("Aquí la 2 y la 3 son equivalentes, y la 4 está a su derecha:")),
                    pair(pic("moveEquivalent", 1), QStringLiteral("La 2, llevada a la derecha de la 3 sin pasar la 4: "
                                                                  "solo ella cambia de sitio."),
                         pic("moveEquivalent", 2), QStringLiteral("Al soltarla, la cadena queda 3 – 2.")),
                    pair(pic("moveEquivalent", 3), QStringLiteral("La 3, llevada más allá de la 4: la sigue toda la cadena."),
                         pic("moveEquivalent", 4), QStringLiteral("La cadena entera queda a la derecha de la 4.")),
                    text(QStringLiteral("Llevada a <b>otro nivel</b>, se lleva también la cadena entera, porque las cajas "
                                        "equivalentes siempre comparten nivel.")),
                    note(QStringLiteral("Al cambiar el orden, las líneas unen las cajas que quedan vecinas, y cada una "
                                        "conserva con qué cajas es segura (ver <b>Línea de equivalencia continua o "
                                        "discontinua</b>).")),
                },
                QStringLiteral("mover arrastrar reordenar equivalencia cadena orden") });

            // ── Connection menu ─────────────────────────────────────────────
            t.push_back({ "createBetween", CONNECTION_MENU, QStringLiteral("Crear caja entre"), I::BoxBetween,
                QStringLiteral("Inserta una caja nueva en medio de la conexión."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea (se resalta la conexión entera) y elige <b>Crear caja "
                                        "entre</b>. Las cajas de arriba quedan unidas a la nueva, y la nueva a las de "
                                        "abajo, que bajan un nivel para hacerle sitio.")),
                    pair(pic("createBetween", 1), QStringLiteral("Clic en la conexión que une 1, 2 y 3 › Crear caja entre."),
                         pic("createBetween", 2), QStringLiteral("La 4 queda entre la 1 y las cajas 2 y 3.")),
                },
                QStringLiteral("intermedia insertar medio") });

            t.push_back({ "forkUpNew", CONNECTION_MENU, QStringLiteral("Bifurcar arriba con caja nueva"), I::ForkUpNew,
                QStringLiteral("Añade a la conexión una caja nueva por arriba."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea y elige <b>Bifurcar arriba con caja nueva</b>. La caja "
                                        "nueva se une a la conexión junto a las que ya estaban arriba, así que queda por "
                                        "encima de todas las cajas de abajo.")),
                    pair(pic("forkUpNew", 1), QStringLiteral("Clic en la conexión › Bifurcar arriba con caja nueva."),
                         pic("forkUpNew", 2), QStringLiteral("La 1 y la 4 están ahora por encima de la 2 y la 3.")),
                },
                QStringLiteral("añadir arriba origen nueva") });

            t.push_back({ "forkDownNew", CONNECTION_MENU, QStringLiteral("Bifurcar abajo con caja nueva"), I::ForkDownNew,
                QStringLiteral("Añade a la conexión una caja nueva por abajo."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea y elige <b>Bifurcar abajo con caja nueva</b>. La caja "
                                        "nueva se une a la conexión junto a las que ya estaban abajo, así que queda por "
                                        "debajo de todas las cajas de arriba.")),
                    pair(pic("forkDownNew", 1), QStringLiteral("Clic en la conexión › Bifurcar abajo con caja nueva."),
                         pic("forkDownNew", 2), QStringLiteral("La 4 se suma a la 2 y la 3, debajo de la 1.")),
                },
                QStringLiteral("añadir abajo destino nueva") });

            t.push_back({ "addSource", CONNECTION_MENU, QStringLiteral("Bifurcar arriba con caja existente"), I::ForkUpExisting,
                QStringLiteral("Añade a la conexión, por arriba, una caja que ya existe."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea, elige <b>Bifurcar arriba con caja existente</b> y haz "
                                        "clic en la caja: quedará por encima de todas las cajas de abajo de la conexión. "
                                        "Si hace falta, esas cajas bajan de nivel.")),
                    pair(pic("addSource", 1),
                            QStringLiteral("Clic en la línea de 1 con 2 › Bifurcar arriba con caja existente; después, clic en la caja 3."),
                         pic("addSource", 2), QStringLiteral("Una sola conexión une la 1 y la 3 con la 2.")),
                    heading(QStringLiteral("Reagrupar conexiones")),
                    text(QStringLiteral("Sirve también para <b>juntar conexiones</b>: si la caja ya llegaba a esas mismas "
                                        "cajas por otra línea, esa unión pasa a esta conexión. Así, donde había dos "
                                        "líneas queda una sola que aúna las tres cajas.")),
                    text(QStringLiteral("Aquí la 1 y la 2 llegan a la 3 por dos líneas distintas. Haz clic en una de ellas "
                                        "(cualquiera vale), elige <b>Bifurcar arriba con caja existente</b> y selecciona "
                                        "la otra caja de arriba:")),
                    picture(pic("addSource", 3), QStringLiteral("Dos conexiones: de la 1 a la 3 y de la 2 a la 3.")),
                    pair(pic("addSource", 4), QStringLiteral("Desde la línea de la 1 se elige la caja 2."),
                         pic("addSource", 5), QStringLiteral("Una sola conexión une la 1 y la 2 con la 3.")),
                    text(QStringLiteral("También sirve para <b>redistribuir conexiones</b>. Para que la caja 2 participe de la "
                                        "conexión que une a las cajas 1, 4 y 5, haz clic en esa línea, elige "
                                        "<b>Bifurcar arriba con caja existente</b> y elige la caja 2:")),
                    pair(pic("addSource", 6), QStringLiteral("Antes: la 1 llega a la 4 y la 5 por una conexión."),
                         pic("addSource", 7), QStringLiteral("Después: la 2 forma parte de esa conexión.")),
                },
                QStringLiteral("añadir arriba origen existente reagrupar juntar unir líneas") });

            t.push_back({ "addTarget", CONNECTION_MENU, QStringLiteral("Bifurcar abajo con caja existente"), I::ForkDownExisting,
                QStringLiteral("Añade a la conexión, por abajo, una caja que ya existe."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea, elige <b>Bifurcar abajo con caja existente</b> y haz "
                                        "clic en la caja: quedará por debajo de todas las cajas de arriba de la "
                                        "conexión. Si hace falta, baja de nivel, con todo lo que tiene debajo.")),
                    pair(pic("addTarget", 1),
                            QStringLiteral("Clic en la línea de 1 con 2 › Bifurcar abajo con caja existente; después, clic en la caja 3."),
                         pic("addTarget", 2), QStringLiteral("Una sola conexión une la 1 con la 2 y la 3.")),
                    heading(QStringLiteral("Reagrupar conexiones")),
                    text(QStringLiteral("Sirve también para <b>juntar conexiones</b>: si la caja ya estaba unida a esas "
                                        "mismas cajas por otra línea, esa unión pasa a esta conexión, y donde había dos "
                                        "líneas queda una sola que aúna las tres cajas.")),
                    text(QStringLiteral("Aquí la 1 llega a la 2 y a la 3 por dos líneas distintas. Haz clic en una de ellas "
                                        "(cualquiera vale), elige <b>Bifurcar abajo con caja existente</b> y selecciona "
                                        "la otra caja de abajo:")),
                    picture(pic("addTarget", 3), QStringLiteral("Dos conexiones: de la 1 a la 2 y de la 1 a la 3.")),
                    pair(pic("addTarget", 4), QStringLiteral("Desde la línea de la 2 se elige la caja 3."),
                         pic("addTarget", 5), QStringLiteral("Una sola conexión une la 1 con la 2 y la 3.")),
                    text(QStringLiteral("También sirve para <b>redistribuir conexiones</b>. Para que la caja 5 participe de la "
                                        "conexión que une a las cajas 1, 2 y 4, haz clic en esa línea, elige "
                                        "<b>Bifurcar abajo con caja existente</b> y elige la caja 5:")),
                    pair(pic("addTarget", 6), QStringLiteral("Antes: la 1 y la 2 llegan a la 4 por una conexión."),
                         pic("addTarget", 7), QStringLiteral("Después: la 5 forma parte de esa conexión.")),
                },
                QStringLiteral("añadir abajo destino existente reagrupar juntar unir líneas") });

            t.push_back({ "lineStyle", CONNECTION_MENU, QStringLiteral("Línea continua o discontinua"), I::LineDashed,
                QStringLiteral("Dibuja la conexión entera discontinua, para marcarla como dudosa, o de nuevo continua."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea y elige <b>Usar línea discontinua</b>; para volver, "
                                        "<b>Usar línea continua</b>. Volver a la línea continua quita también las marcas "
                                        "de conexión hipotética que tuvieran sus cajas.")),
                    pair(pic("lineStyle", 1), QStringLiteral("Clic en la conexión › Usar línea discontinua."),
                         pic("lineStyle", 2), QStringLiteral("Toda la conexión se dibuja discontinua.")),
                },
                QStringLiteral("dudosa hipotética discontinua continua estilo") });

            t.push_back({ "simplify", CONNECTION_MENU, QStringLiteral("Simplificar conexión"), I::Simplify,
                QStringLiteral("Quita una caja de la conexión, sin tocar el resto."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea, elige <b>Simplificar conexión</b> y haz clic en una de "
                                        "sus cajas: deja de formar parte de la conexión. Las demás siguen unidas.")),
                    triple(pic("simplify", 1), QStringLiteral("Clic en la conexión › Simplificar conexión."),
                           pic("simplify", 2), QStringLiteral("Se elige la 3."),
                           pic("simplify", 3), QStringLiteral("La conexión une solo la 1 y la 2.")),
                    note(QStringLiteral("Una caja que pierde lo que tenía encima puede subir de nivel.")),
                },
                QStringLiteral("quitar caja conexión reducir") });

            t.push_back({ "removeConnection", CONNECTION_MENU, QStringLiteral("Eliminar conexión"), I::RemoveConnection,
                QStringLiteral("Borra la conexión entera."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic sobre la línea y elige <b>Eliminar conexión</b>: ninguna de sus cajas "
                                        "queda unida por ella. Las cajas que se quedan sin nada por encima suben al "
                                        "primer nivel.")),
                    pair(pic("removeConnection", 1), QStringLiteral("Clic en la conexión › Eliminar conexión."),
                         pic("removeConnection", 2), QStringLiteral("Las tres cajas quedan sueltas.")),
                    note(QStringLiteral("Una caja que pierde lo que tenía encima puede subir de nivel.")),
                },
                QStringLiteral("borrar quitar línea") });

            // ── Joint diagram ───────────────────────────────────────────────
            t.push_back({ "jointManage", JOINT, QStringLiteral("Administrar esquemas"), I::Joint,
                QStringLiteral("Añade esquemas al esquema conjunto o quítalos."),
                {
                    heading(HOW),
                    text(QStringLiteral("En el esquema conjunto, haz clic derecho en el fondo: aparece la lista de "
                                        "esquemas del proyecto, con su estado. El botón <b>+</b> añade un esquema donde "
                                        "hiciste clic; la papelera lo quita.")),
                    picture(QStringLiteral(":/tutorial/joint_manage.png"),
                            QStringLiteral("El «Esquema 1» ya está; el «Esquema 2» se puede añadir.")),
                    note(QStringLiteral("Cada esquema entra como copia: lo que hagas en el esquema conjunto (conectar o "
                                        "fusionar cajas de esquemas distintos, mover o quitar bloques) no cambia el "
                                        "original.")),
                },
                QStringLiteral("conjunto añadir quitar esquemas") });

            t.push_back({ "moveDiagram", JOINT, QStringLiteral("Mover esquema"), I::Move,
                QStringLiteral("Lleva un esquema entero a otro sitio del esquema conjunto: todos sus bloques a la vez."),
                {
                    heading(HOW),
                    text(QStringLiteral("En el esquema conjunto, haz clic derecho sobre una caja y elige <b>Mover esquema "
                                        "«…»</b> (el menú muestra el nombre de su esquema). El esquema entero queda "
                                        "colgando del ratón: una silueta discontinua muestra dónde caerá y una etiqueta "
                                        "indica en qué nivel empezará. Haz clic para soltarlo; con clic derecho o "
                                        "<b>Esc</b> vuelve a su sitio.")),
                    picture(pic("moveDiagram", 1),
                            QStringLiteral("Clic derecho en la caja 3 › Mover esquema «Esquema 1». Debajo está "
                                           "también Mover bloque.")),
                    heading(QStringLiteral("Esquema o bloque")),
                    text(QStringLiteral("Un esquema puede tener <b>varios bloques</b>: grupos de cajas sin conexión entre "
                                        "ellos. <b>Mover bloque</b> se lleva solo el bloque de la caja en la que hiciste "
                                        "clic; <b>Mover esquema</b> se lleva <b>todos los bloques del esquema a la vez</b>, "
                                        "y conserva cómo estaban colocados unos respecto a otros.")),
                    text(QStringLiteral("Aquí el «Esquema 1» tiene dos bloques, 1–2 y 3–4. Con <b>Mover esquema</b> "
                                        "se mueven los dos juntos, a la derecha del «Esquema 2»:")),
                    pair(pic("moveDiagram", 2), QStringLiteral("Los dos bloques del «Esquema 1» cuelgan juntos."),
                         pic("moveDiagram", 3), QStringLiteral("Soltados juntos a la derecha del «Esquema 2».")),
                    note(QStringLiteral("La opción solo aparece mientras el esquema no está mezclado con otros: en cuanto "
                                        "una de sus cajas se conecta o se fusiona con una de otro esquema, ya no se puede "
                                        "mover como un todo y queda <b>Mover bloque</b>. Si el esquema tiene un solo "
                                        "bloque, solo aparece Mover esquema, porque los dos harían lo mismo.")),
                },
                QStringLiteral("mover esquema entero conjunto bloques todos") });

            t.push_back({ "removeDiagram", JOINT, QStringLiteral("Quitar esquema"), I::TakeOut,
                QStringLiteral("Saca del esquema conjunto todas las cajas de un esquema, sin cambiar el original."),
                {
                    heading(HOW),
                    text(QStringLiteral("En el esquema conjunto, haz clic derecho sobre una caja o conexión y elige <b>Quitar esquema "
                                        "«…»</b> (el menú muestra el nombre de su esquema) y confirma. También se puede "
                                        "con la papelera de <b>Administrar esquemas</b>, en el menú del fondo. El "
                                        "esquema original no cambia y puedes volver a añadirlo.")),
                    pair(pic("removeDiagram", 1), QStringLiteral("Clic derecho en la caja 3 › Quitar esquema «Esquema 1»."),
                         pic("removeDiagram", 2), QStringLiteral("Solo queda el «Esquema 2».")),
                    heading(QStringLiteral("Esquema o bloque")),
                    text(QStringLiteral("<ul style=\"margin-left:-18px\">"
                                        "<li><b>Quitar esquema</b> quita todas las cajas de ese esquema. Aparece mientras "
                                        "el esquema no esté conectado ni fusionado con otros.</li>"
                                        "<li><b>Quitar bloque</b> quita la caja o conexión y todo lo que está unido a "
                                        "ella, aunque venga de otros esquemas.</li></ul>")),
                },
                QStringLiteral("quitar esquema conjunto bloque sacar borrar") });

            // ── Tabs ────────────────────────────────────────────────────────
            t.push_back({ "duplicateDiagram", TABS, QStringLiteral("Duplicar esquema"), I::Duplicate,
                QStringLiteral("Crea un esquema nuevo que es una copia exacta de otro."),
                {
                    heading(HOW),
                    text(QStringLiteral("Haz clic derecho sobre la pestaña del esquema y elige <b>Duplicar esquema</b> "
                                        "(o usa <b>Esquema › Duplicar esquema</b>, <b>Ctrl+D</b>, para el esquema "
                                        "abierto). La copia aparece en una pestaña nueva al final, con el nombre "
                                        "«<i>nombre</i> (copia)», y se abre.")),
                    pair(pic("duplicateDiagram", 1), QStringLiteral("Clic derecho en la pestaña › Duplicar esquema."),
                         pic("duplicateDiagram", 2), QStringLiteral("La copia, «Esquema (copia)», se abre en una pestaña nueva.")),
                    note(QStringLiteral("La copia es un esquema independiente: cambiar uno no afecta al otro, y se puede "
                                        "añadir al esquema conjunto por separado. Para llevar solo una parte a otro "
                                        "esquema, usa <b>Copiar bloque o esquema</b> y <b>Pegar</b>.")),
                },
                QStringLiteral("duplicar copiar clonar pestaña ctrl+d") });

            // ── Project ─────────────────────────────────────────────────────
            t.push_back({ "projectSettings", PROJECT, QStringLiteral("Ajustes del proyecto"), I::Settings,
                QStringLiteral("Decide cómo son las cajas y equivalencias nuevas, las distancias del dibujo y el "
                               "tamaño por defecto de cada forma."),
                {
                    heading(HOW),
                    text(QStringLiteral("Abre el menú <b>Proyecto</b> y elige <b>Ajustes del proyecto…</b> (o pulsa "
                                        "<b>Ctrl+,</b>). Cambia lo que quieras y pulsa <b>Aceptar</b>: se aplica a todos "
                                        "los esquemas del proyecto, también al esquema conjunto.")),
                    picture(pic("projectSettings", 1), QStringLiteral("Los ajustes de un proyecto.")),
                    heading(QStringLiteral("Qué se puede ajustar")),
                    text(QStringLiteral("<ul style=\"margin-left:-18px\">"
                                        "<li><b>Cajas nuevas</b>: su forma, sus colores y su tamaño de fuente. Solo para "
                                        "las que se creen a partir de ahora; las que ya existen no cambian. La vista "
                                        "previa muestra cómo serán.</li>"
                                        "<li><b>Equivalencias nuevas</b>: si se dibujan con línea continua o discontinua.</li>"
                                        "<li><b>Distancias</b>: entre cajas vecinas de un mismo nivel, entre las cajas y "
                                        "sus conexiones, y entre las líneas horizontales que se apilan entre dos niveles. "
                                        "Todos los esquemas se redibujan con ellas.</li>"
                                        "<li><b>Tamaño por defecto</b> de cada forma: el de las cajas que no tienen un "
                                        "tamaño propio (ver <b>Cambiar el tamaño de una caja</b>).</li></ul>")),
                    heading(QStringLiteral("Las cajas que ya existen")),
                    text(QStringLiteral("Si cambias el tamaño por defecto de una forma y hay cajas que lo usan, al aceptar "
                                        "se te pregunta qué hacer con ellas: con <b>Solo las cajas nuevas</b> se quedan "
                                        "con el tamaño que tenían; con <b>También las existentes</b> toman el nuevo.")),
                    picture(pic("projectSettings", 2), QStringLiteral("La pregunta, al cambiar un tamaño por defecto.")),
                    heading(QStringLiteral("Predeterminados de la aplicación")),
                    text(QStringLiteral("Marca <b>Usar estos ajustes en los proyectos nuevos</b> para que todo proyecto "
                                        "nuevo empiece con ellos. <b>Cargar los de la aplicación</b> rellena el formulario "
                                        "con esos ajustes, y <b>Valores de fábrica</b>, con los que trae la aplicación.")),
                    text(QStringLiteral("Para que un proyecto que ya tienes adopte los predeterminados de la aplicación de "
                                        "una vez, elige <b>Proyecto › Aplicar los ajustes predeterminados de la "
                                        "aplicación</b>.")),
                    note(QStringLiteral("Los ajustes se guardan con el proyecto. Cambiarlos no se deshace con <b>Ctrl+Z</b>: "
                                        "para volver atrás, abre de nuevo los ajustes y deja los valores como estaban.")),
                },
                QStringLiteral("ajustes configuración preferencias predeterminados defecto distancias separación "
                               "tamaño forma color fuente equivalencias nuevo proyecto ctrl+,") });

            return t;
        }

        const std::vector<Topic>& topics() {
            static const std::vector<Topic> all = buildTopics();
            return all;
        }

        const Topic* findTopic(const QString& id) {
            for (const Topic& t : topics()) if (t.id == id) return &t;
            return nullptr;
        }

        // Lower case, without accents: what the search compares.
        QString folded(const QString& s) {
            QString out;
            for (const QChar c : s.normalized(QString::NormalizationForm_D))
                if (c.category() != QChar::Mark_NonSpacing) out += c.toLower();
            return out;
        }

        // ============================================================================
        // The "?" next to a menu option
        // ============================================================================

        class OptionHelpButton : public QAbstractButton {
        public:
            explicit OptionHelpButton(QWidget* parent) : QAbstractButton(parent) {
                setFixedSize(SIZE, SIZE);
                setCursor(Qt::PointingHandCursor);
                setToolTip(QStringLiteral("¿Qué hace esta opción?"));
                setFocusPolicy(Qt::NoFocus);
            }
            static constexpr int SIZE = 20;

        protected:
            void enterEvent(QEnterEvent*) override { hovered_ = true; update(); }
            void leaveEvent(QEvent*) override { hovered_ = false; update(); }
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
                p.setPen(QPen(hovered_ ? style::palette::accent : QColor(0xD3, 0xD7, 0xE2), 1.2));
                p.setBrush(hovered_ ? style::palette::accent_soft : QColor(Qt::white));
                p.drawEllipse(r);
                QFont f = font();
                f.setPixelSize(11);
                f.setBold(true);
                p.setFont(f);
                p.setPen(hovered_ ? style::palette::accent_dark : style::palette::muted);
                p.drawText(rect(), Qt::AlignCenter, QStringLiteral("?"));
            }

        private:
            bool hovered_ = false;
        };

        // The submenu arrow, drawn by hand so it can sit left of the "?".
        class SubmenuChevron : public QWidget {
        public:
            explicit SubmenuChevron(QWidget* parent) : QWidget(parent) {
                setFixedSize(12, 20);
                setAttribute(Qt::WA_TransparentForMouseEvents);
            }

        protected:
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QPen(style::palette::ink_soft, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                const QPointF c(width() / 2.0, height() / 2.0);
                p.drawPolyline(QPolygonF({ c + QPointF(-2, -4), c + QPointF(2, 0), c + QPointF(-2, 4) }));
            }
        };

        // Puts the buttons in place once the menu knows where its options are.
        class HelpButtonsPlacer : public QObject {
        public:
            HelpButtonsPlacer(QMenu* menu, std::vector<std::pair<QAction*, QString>> entries, QWidget* dialog_parent)
                : QObject(menu), menu_(menu), entries_(std::move(entries)), parent_(dialog_parent)
            {
                menu->installEventFilter(this);
            }

        protected:
            bool eventFilter(QObject* watched, QEvent* event) override {
                if (watched != menu_) return QObject::eventFilter(watched, event);
                if (event->type() == QEvent::Show) place();
                // While a submenu is open it takes every click, and hands the
                // presses on this menu to the menu itself (not the releases),
                // never to a "?" on it (the one of the submenu's own option,
                // say): such a press on a "?" clicks it here.
                if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
                    const QPoint at = static_cast<QMouseEvent*>(event)->position().toPoint();
                    for (const auto& [action, button] : buttons_) {
                        if (!button->isVisible() || !button->geometry().contains(at)) continue;
                        if (event->type() == QEvent::MouseButtonPress) button->click();
                        return true;
                    }
                }
                return QObject::eventFilter(watched, event);
            }

        private:
            void place() {
                for (const auto& [action, topic] : entries_) {
                    const QRect r = menu_->actionGeometry(action);
                    if (r.isEmpty()) continue;
                    auto*& button = buttons_[action];
                    if (!button) {
                        button = new OptionHelpButton(menu_);
                        const QString id = topic;
                        QPointer<QWidget> parent = parent_;
                        QPointer<QMenu> menu = menu_;
                        QObject::connect(button, &QAbstractButton::clicked, menu_, [id, parent, menu] {
                            if (menu) menu->close();
                            QTimer::singleShot(0, [id, parent] { showOptionsHelp(parent, id); });
                        });
                    }
                    // All in one column at the right end. A submenu's arrow
                    // (hidden from the menu itself) goes just left of its "?".
                    const int left = r.right() - 14 - OptionHelpButton::SIZE;
                    button->move(left, r.center().y() - OptionHelpButton::SIZE / 2 + 1);
                    button->show();
                    button->raise();
                    if (action->menu()) {
                        auto*& chevron = chevrons_[action];
                        if (!chevron) chevron = new SubmenuChevron(menu_);
                        chevron->move(left - chevron->width() - 14, r.center().y() - chevron->height() / 2 + 1);
                        chevron->show();
                        chevron->raise();
                    }
                }
            }

            QMenu* menu_;
            std::vector<std::pair<QAction*, QString>> entries_;
            QPointer<QWidget> parent_;
            std::map<QAction*, OptionHelpButton*> buttons_;
            std::map<QAction*, SubmenuChevron*> chevrons_;
        };

        // ============================================================================
        // The window
        // ============================================================================

        // A screenshot fitted into `max_width` (never enlarged), with rounded
        // corners and a hairline border; a click shows it full screen.
        class HelpPicture : public QWidget {
        public:
            HelpPicture(const QString& resource, int max_width, int max_height, QWidget* parent)
                : QWidget(parent), source_(resource)
            {
                const qreal dpr = devicePixelRatioF();
                QSizeF logical = source_.isNull() ? QSizeF(max_width, 120) : QSizeF(source_.size()) / std::max(1.0, dpr);
                logical = logical.scaled(QSizeF(std::min<double>(logical.width(), max_width),
                                                std::min<double>(logical.height(), max_height)), Qt::KeepAspectRatio);
                setFixedSize(logical.toSize());
                if (!source_.isNull()) {
                    setCursor(Qt::PointingHandCursor);
                    setToolTip(QStringLiteral("Haz clic para ampliar"));
                }
            }

        protected:
            void enterEvent(QEnterEvent*) override { hovered_ = true; update(); }
            void leaveEvent(QEvent*) override { hovered_ = false; update(); }
            void mousePressEvent(QMouseEvent* event) override { event->accept(); }
            void mouseReleaseEvent(QMouseEvent* event) override {
                if (event->button() == Qt::LeftButton && !source_.isNull() && rect().contains(event->pos()))
                    showPictureFullScreen(source_, window());
            }
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
                QPainterPath clip;
                clip.addRoundedRect(r, 10, 10);
                p.fillPath(clip, Qt::white);
                if (source_.isNull()) {
                    p.setPen(style::palette::muted);
                    p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Imagen no disponible"));
                }
                else {
                    const qreal dpr = devicePixelRatioF();
                    if (fitted_.size() != size() * dpr) {
                        fitted_ = source_.scaled(size() * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        fitted_.setDevicePixelRatio(dpr);
                    }
                    p.save();
                    p.setClipPath(clip);
                    p.drawPixmap(QPointF(0, 0), fitted_);
                    p.restore();
                }
                p.setPen(QPen(hovered_ ? style::palette::accent : style::palette::line, hovered_ ? 1.6 : 1.0));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, 10, 10);
            }

        private:
            QPixmap source_;
            QPixmap fitted_;
            bool hovered_ = false;
        };

        class OptionsHelpWindow : public QDialog {
        public:
            explicit OptionsHelpWindow(QWidget* parent) : QDialog(parent) {
                setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
                setAttribute(Qt::WA_TranslucentBackground);
                setWindowTitle(QStringLiteral("Ayuda de las opciones"));
                setModal(true);

                // As big as the screen comfortably allows.
                QScreen* screen = parent && parent->window()->screen() ? parent->window()->screen()
                                                                      : QGuiApplication::primaryScreen();
                const QRect avail = screen->availableGeometry();
                const int width = std::min(1120, static_cast<int>(avail.width() * 0.9));
                const int height = std::min(800, static_cast<int>(avail.height() * 0.9));
                setFixedSize(width, height);

                setStyleSheet(dialogs::styleSheet() + QStringLiteral(R"(
QFrame#helpCard { background: white; border: 1px solid #E3E6EF; border-radius: 16px; }
QLabel { background: transparent; }
QLabel#helpTitle { color: #1F2330; font-size: 13pt; font-weight: 700; }
QLabel#helpSubtitle { color: #8A90A2; font-size: 9.5pt; }
QLineEdit#helpSearch {
    background: #F5F6FA; border: 1px solid #E3E6EF; border-radius: 9px; padding: 6px 10px; color: #1F2330;
}
QLineEdit#helpSearch:focus { border-color: #6366F1; background: white; }
QPushButton#helpClose {
    background: transparent; border: none; border-radius: 9px; color: #8A90A2; font-size: 15pt; padding: 0;
}
QPushButton#helpClose:hover { background: #F1F2F7; color: #1F2330; }
QListWidget#helpTopics {
    background: #F5F6FA; border: none; border-radius: 12px; padding: 8px 6px; outline: none;
}
QListWidget#helpTopics::item { padding: 0 8px; border-radius: 8px; color: #1F2330; }
QListWidget#helpTopics::item:hover { background: #ECEEF5; }
QListWidget#helpTopics::item:selected { background: #E4E7FF; color: #4F46E5; }
QListWidget#helpTopics::item:disabled {
    color: #8A90A2; background: transparent; font-size: 7.5pt; font-weight: 700; padding: 8px 8px 0 8px;
}
QScrollArea#helpContent { background: transparent; border: none; }
QScrollArea#helpContent > QWidget > QWidget { background: transparent; }
QLabel#topicTitle { color: #1F2330; font-size: 16pt; font-weight: 700; }
QLabel#topicSummary { color: #4B5068; font-size: 11pt; }
QLabel#topicHeading { color: #6366F1; font-size: 8pt; font-weight: 700; letter-spacing: 1px; padding-top: 6px; }
QLabel#topicText { color: #3A4050; font-size: 10.5pt; }
QLabel#topicCaption { color: #8A90A2; font-size: 9pt; }
QFrame#topicNote { background: #F5F6FA; border: 1px solid #E3E6EF; border-radius: 10px; }
QLabel#topicNoteText { color: #4B5068; font-size: 10pt; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #D6DAE4; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #B8BDCB; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)"));

                auto* outer = new QVBoxLayout(this);
                outer->setContentsMargins(22, 18, 22, 26);
                auto* card = new QFrame(this);
                card->setObjectName("helpCard");
                auto* shadow = new QGraphicsDropShadowEffect(card);
                shadow->setBlurRadius(34);
                shadow->setOffset(0, 8);
                shadow->setColor(QColor(31, 35, 48, 70));
                card->setGraphicsEffect(shadow);
                outer->addWidget(card);

                auto* column = new QVBoxLayout(card);
                column->setContentsMargins(24, 20, 20, 20);
                column->setSpacing(16);

                // ── Header ────────────────────────────────────────────────────
                auto* head = new QHBoxLayout;
                head->setSpacing(14);
                auto* badge = new QLabel(card);
                badge->setPixmap(badgePixmap(devicePixelRatioF()));
                head->addWidget(badge, 0, Qt::AlignTop);
                auto* titles = new QVBoxLayout;
                titles->setSpacing(2);
                auto* title = new QLabel(QStringLiteral("Ayuda de las opciones"), card);
                title->setObjectName("helpTitle");
                auto* subtitle = new QLabel(QStringLiteral("Qué hace cada opción de los menús de las cajas, las conexiones "
                                                           "y el fondo, con ejemplos."), card);
                subtitle->setObjectName("helpSubtitle");
                titles->addWidget(title);
                titles->addWidget(subtitle);
                head->addLayout(titles, 1);
                search_ = new QLineEdit(card);
                search_->setObjectName("helpSearch");
                search_->setPlaceholderText(QStringLiteral("Buscar una opción…"));
                search_->setClearButtonEnabled(true);
                search_->setFixedWidth(250);
                head->addWidget(search_, 0, Qt::AlignVCenter);
                auto* close = new QPushButton(QStringLiteral("×"), card);
                close->setObjectName("helpClose");
                close->setFixedSize(34, 34);
                close->setCursor(Qt::PointingHandCursor);
                close->setToolTip(QStringLiteral("Cerrar (Esc)"));
                close->setAutoDefault(false);
                connect(close, &QPushButton::clicked, this, &QDialog::reject);
                head->addWidget(close, 0, Qt::AlignVCenter);
                column->addLayout(head);

                // ── Body: options on the left, the chosen one on the right ────
                auto* body = new QHBoxLayout;
                body->setSpacing(20);
                list_ = new QListWidget(card);
                list_->setObjectName("helpTopics");
                list_->setFixedWidth(SIDEBAR_WIDTH);
                list_->setIconSize(QSize(18, 18));
                list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
                fillList();
                body->addWidget(list_);

                content_ = new QScrollArea(card);
                content_->setObjectName("helpContent");
                content_->setWidgetResizable(true);
                content_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                content_->setFrameShape(QFrame::NoFrame);
                body->addWidget(content_, 1);
                column->addLayout(body, 1);

                content_width_ = width - 22 * 2 - 24 - 20 - SIDEBAR_WIDTH - 20 - 14 - 16;

                connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
                    if (item && !item->data(Qt::UserRole).toString().isEmpty())
                        showTopic(item->data(Qt::UserRole).toString());
                });
                connect(search_, &QLineEdit::textChanged, this, [this](const QString& query) { filter(query); });
            }

            void open(const QString& topic) {
                const QString id = findTopic(topic) ? topic : topics().front().id;
                for (int i = 0; i < list_->count(); ++i)
                    if (list_->item(i)->data(Qt::UserRole).toString() == id) {
                        list_->setCurrentRow(i);
                        list_->scrollToItem(list_->item(i), QAbstractItemView::PositionAtCenter);
                        break;
                    }
            }

            void setVisible(bool visible) override {
                if (visible && !isVisible())
                    if (QWidget* host = parentWidget() ? parentWidget()->window() : nullptr)
                        move(host->geometry().center() - rect().center());
                QDialog::setVisible(visible);
            }

        protected:
            // Dragged by any bare part of the card.
            void mousePressEvent(QMouseEvent* event) override {
                if (event->button() == Qt::LeftButton) {
                    dragging_ = true;
                    drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
                }
                QDialog::mousePressEvent(event);
            }
            void mouseMoveEvent(QMouseEvent* event) override {
                if (dragging_) move(event->globalPosition().toPoint() - drag_offset_);
                QDialog::mouseMoveEvent(event);
            }
            void mouseReleaseEvent(QMouseEvent* event) override {
                dragging_ = false;
                QDialog::mouseReleaseEvent(event);
            }

        private:
            static constexpr int SIDEBAR_WIDTH = 290;

            static QPixmap badgePixmap(qreal dpr) {
                QPixmap pm(QSize(44, 44) * dpr);
                pm.setDevicePixelRatio(dpr);
                pm.fill(Qt::transparent);
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                const QPointF c(22, 22);
                p.setPen(Qt::NoPen);
                p.setBrush(style::palette::accent_soft);
                p.drawEllipse(c, 22, 22);
                QLinearGradient g(c - QPointF(14, 14), c + QPointF(14, 14));
                g.setColorAt(0, style::palette::accent.lighter(112));
                g.setColorAt(1, style::palette::violet);
                p.setBrush(g);
                p.drawEllipse(c, 14, 14);
                QFont f;
                f.setPixelSize(17);
                f.setBold(true);
                p.setFont(f);
                p.setPen(Qt::white);
                p.drawText(QRectF(c.x() - 14, c.y() - 14, 28, 28), Qt::AlignCenter, QStringLiteral("?"));
                return pm;
            }

            void fillList() {
                QString group;
                for (const Topic& t : topics()) {
                    if (t.group != group) {
                        group = t.group;
                        auto* header = new QListWidgetItem(group.toUpper(), list_);
                        header->setFlags(Qt::NoItemFlags);
                        QFont f = list_->font();
                        f.setPointSizeF(7.5);
                        f.setBold(true);
                        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
                        header->setFont(f);
                        header->setForeground(style::palette::muted);
                        header->setData(Qt::UserRole + 1, group);
                        header->setSizeHint(QSize(SIDEBAR_WIDTH - 20, list_->count() > 1 ? 40 : 30));
                    }
                    auto* item = new QListWidgetItem(style::icon(t.icon), t.title, list_);
                    item->setData(Qt::UserRole, t.id);
                    item->setData(Qt::UserRole + 1, t.group);
                    item->setToolTip(t.summary);
                    item->setSizeHint(QSize(SIDEBAR_WIDTH - 20, 38));
                }
            }

            void filter(const QString& query) {
                const QString q = folded(query.trimmed());
                std::map<QString, bool> group_shown;
                for (int i = 0; i < list_->count(); ++i) {
                    QListWidgetItem* item = list_->item(i);
                    const QString id = item->data(Qt::UserRole).toString();
                    if (id.isEmpty()) continue;
                    const Topic* t = findTopic(id);
                    const bool match = q.isEmpty() || folded(t->title + " " + t->summary + " " + t->keywords).contains(q);
                    item->setHidden(!match);
                    if (match) group_shown[t->group] = true;
                }
                for (int i = 0; i < list_->count(); ++i) {
                    QListWidgetItem* item = list_->item(i);
                    if (item->data(Qt::UserRole).toString().isEmpty())
                        item->setHidden(!group_shown[item->data(Qt::UserRole + 1).toString()]);
                }
                // The first match on screen, when the one shown was filtered out.
                QListWidgetItem* current = list_->currentItem();
                if (!q.isEmpty() && (!current || current->isHidden())) {
                    for (int i = 0; i < list_->count(); ++i) {
                        QListWidgetItem* item = list_->item(i);
                        if (!item->isHidden() && !item->data(Qt::UserRole).toString().isEmpty()) {
                            list_->setCurrentItem(item);
                            break;
                        }
                    }
                }
            }

            QLabel* label(const QString& text, const char* name, QWidget* parent) {
                auto* l = new QLabel(text, parent);
                l->setObjectName(name);
                l->setWordWrap(true);
                l->setTextFormat(Qt::RichText);
                l->setFixedWidth(content_width_);
                return l;
            }

            // A picture and its caption, stacked.
            QWidget* captioned(const QString& image, const QString& caption, int max_width, QWidget* parent) {
                auto* box = new QWidget(parent);
                auto* col = new QVBoxLayout(box);
                col->setContentsMargins(0, 0, 0, 0);
                col->setSpacing(6);
                auto* pic = new HelpPicture(image, max_width, 360, box);
                col->addWidget(pic, 0, Qt::AlignHCenter);
                auto* cap = new QLabel(caption, box);
                cap->setObjectName("topicCaption");
                cap->setWordWrap(true);
                cap->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
                cap->setFixedWidth(std::max(pic->width(), std::min(max_width, 240)));
                // Wrapped: its height for that width, or the box comes out short.
                cap->setFixedHeight(cap->heightForWidth(cap->width()));
                col->addWidget(cap, 0, Qt::AlignHCenter);
                box->setFixedSize(col->sizeHint());
                return box;
            }

            void showTopic(const QString& id) {
                const Topic* t = findTopic(id);
                if (!t) return;
                auto* page = new QWidget;
                auto* col = new QVBoxLayout(page);
                col->setContentsMargins(8, 4, 8, 24);
                col->setSpacing(12);

                auto* head = new QHBoxLayout;
                head->setSpacing(12);
                auto* icon = new QLabel(page);
                icon->setPixmap(style::icon(t->icon).pixmap(QSize(30, 30), devicePixelRatioF()));
                head->addWidget(icon, 0, Qt::AlignVCenter);
                auto* title = new QLabel(t->title, page);
                title->setObjectName("topicTitle");
                head->addWidget(title, 1);
                col->addLayout(head);
                col->addWidget(label(t->summary, "topicSummary", page));

                for (const Block& b : t->blocks) {
                    switch (b.kind) {
                    case Block::Kind::Heading:
                        col->addSpacing(4);
                        col->addWidget(label(b.text.toUpper(), "topicHeading", page));
                        break;
                    case Block::Kind::Text:
                        col->addWidget(label(b.text, "topicText", page));
                        break;
                    case Block::Kind::Picture:
                        col->addWidget(captioned(b.first, b.first_caption, std::min(content_width_, 560), page), 0, Qt::AlignHCenter);
                        break;
                    case Block::Kind::Pair:
                    case Block::Kind::Triple: {
                        // The steps side by side, an arrow between each two.
                        std::vector<std::pair<QString, QString>> steps = {
                            { b.first, b.first_caption }, { b.second, b.second_caption } };
                        if (b.kind == Block::Kind::Triple) steps.emplace_back(b.third, b.third_caption);
                        const int n = static_cast<int>(steps.size());
                        const int spacing = n == 2 ? 14 : 8;
                        const int arrow_width = n == 2 ? 26 : 20;
                        const int each = (content_width_ - spacing * 2 * (n - 1) - arrow_width * (n - 1)) / n;
                        auto* row = new QHBoxLayout;
                        row->setSpacing(spacing);
                        row->addStretch();
                        for (int i = 0; i < n; ++i) {
                            if (i > 0) {
                                auto* arrow = new QLabel(QStringLiteral("→"), page);
                                arrow->setStyleSheet(n == 2 ? QStringLiteral("color: #A0A5B5; font-size: 18pt;")
                                                            : QStringLiteral("color: #A0A5B5; font-size: 15pt;"));
                                row->addWidget(arrow, 0, Qt::AlignVCenter);
                            }
                            row->addWidget(captioned(steps[i].first, steps[i].second, each, page), 0, Qt::AlignVCenter);
                        }
                        row->addStretch();
                        col->addLayout(row);
                        break;
                    }
                    case Block::Kind::Note: {
                        auto* frame = new QFrame(page);
                        frame->setObjectName("topicNote");
                        frame->setFixedWidth(content_width_);
                        auto* inner = new QHBoxLayout(frame);
                        inner->setContentsMargins(14, 10, 14, 10);
                        auto* l = new QLabel(QStringLiteral("<b>Ten en cuenta.</b> ") + b.text, frame);
                        l->setObjectName("topicNoteText");
                        l->setWordWrap(true);
                        l->setTextFormat(Qt::RichText);
                        inner->addWidget(l);
                        col->addWidget(frame);
                        break;
                    }
                    }
                }
                col->addStretch();
                content_->setWidget(page); // deletes the previous page
                content_->verticalScrollBar()->setValue(0);
            }

            QLineEdit* search_;
            QListWidget* list_;
            QScrollArea* content_;
            int content_width_ = 600;
            bool dragging_ = false;
            QPoint drag_offset_;
        };

    } // namespace

    void showOptionsHelp(QWidget* parent, const QString& topic) {
        OptionsHelpWindow window(parent);
        window.open(topic);
        window.exec();
    }

    QString topicForMenuEntry(const QString& entry) {
        static const std::map<QString, QString> exact = {
            { QStringLiteral("Nueva caja"), QStringLiteral("newBox") },
            { QStringLiteral("Crear caja arriba"), QStringLiteral("createAbove") },
            { QStringLiteral("Crear caja debajo"), QStringLiteral("createBelow") },
            { QStringLiteral("Crear caja a la izquierda"), QStringLiteral("createSide") },
            { QStringLiteral("Crear caja a la derecha"), QStringLiteral("createSide") },
            { QStringLiteral("Fusionar"), QStringLiteral("fuse") },
            { QStringLiteral("Mover bloque"), QStringLiteral("moveBlock") },
            { QStringLiteral("Mover este esquema"), QStringLiteral("moveDiagram") },
            { QStringLiteral("Conexiones hipotéticas"), QStringLiteral("uncertain") },
            { QStringLiteral("Eliminar conexión parcial"), QStringLiteral("removePartial") },
            { QStringLiteral("Eliminar caja"), QStringLiteral("removeBox") },
            { QStringLiteral("Eliminar bloque"), QStringLiteral("removeBlock") },
            { QStringLiteral("Quitar bloque"), QStringLiteral("removeBlock") },
            { QStringLiteral("Crear caja entre"), QStringLiteral("createBetween") },
            { QStringLiteral("Bifurcar arriba con caja nueva"), QStringLiteral("forkUpNew") },
            { QStringLiteral("Bifurcar abajo con caja nueva"), QStringLiteral("forkDownNew") },
            { QStringLiteral("Bifurcar arriba con caja existente"), QStringLiteral("addSource") },
            { QStringLiteral("Bifurcar abajo con caja existente"), QStringLiteral("addTarget") },
            { QStringLiteral("Usar línea continua"), QStringLiteral("lineStyle") },
            { QStringLiteral("Usar línea discontinua"), QStringLiteral("lineStyle") },
            { QStringLiteral("Simplificar conexión"), QStringLiteral("simplify") },
            { QStringLiteral("Eliminar conexión"), QStringLiteral("removeConnection") },
            { QStringLiteral("Administrar esquemas"), QStringLiteral("jointManage") },
            { QStringLiteral("Pegar"), QStringLiteral("paste") },
            { QStringLiteral("Copiar bloque"), QStringLiteral("copyPiece") },
            { QStringLiteral("Copiar esquema"), QStringLiteral("copyPiece") },
            { QStringLiteral("Copiar este esquema"), QStringLiteral("copyPiece") },
            { QStringLiteral("Duplicar esquema"), QStringLiteral("duplicateDiagram") },
            { QStringLiteral("Hacer equivalente a…"), QStringLiteral("addEquivalence") },
            { QStringLiteral("Quitar equivalencia"), QStringLiteral("removeEquivalence") },
            { QStringLiteral("Quitar este esquema"), QStringLiteral("removeDiagram") },
        };
        const QString text = QString(entry).remove(QLatin1Char('&'));
        if (auto it = exact.find(text); it != exact.end()) return it->second;
        static const QRegularExpression below(QStringLiteral("^Caja «.*» por debajo de$"));
        static const QRegularExpression above(QStringLiteral("^Caja «.*» por encima de$"));
        if (below.match(text).hasMatch()) return QStringLiteral("addChildConnection");
        if (above.match(text).hasMatch()) return QStringLiteral("addParentConnection");
        if (text.startsWith(QStringLiteral("Mover esquema «"))) return QStringLiteral("moveDiagram");
        if (text.startsWith(QStringLiteral("Copiar esquema «"))) return QStringLiteral("copyPiece");
        if (text.startsWith(QStringLiteral("Quitar equivalencia con «"))) return QStringLiteral("removeEquivalence");
        if (text.startsWith(QStringLiteral("Quitar esquema «"))) return QStringLiteral("removeDiagram");
        return {};
    }

    void addHelpButtons(QMenu* menu, QWidget* dialog_parent) {
        std::vector<std::pair<QAction*, QString>> entries;
        for (QAction* a : menu->actions()) {
            if (a->isSeparator() || qobject_cast<QWidgetAction*>(a)) continue;
            const QVariant own = a->property(TOPIC_PROPERTY);
            const QString topic = own.isValid() ? own.toString() : topicForMenuEntry(a->text());
            if (!topic.isEmpty()) entries.emplace_back(a, topic);
        }
        if (entries.empty()) return;
        bool arrows = false;
        for (QAction* a : menu->actions()) arrows = arrows || a->menu();
        // Room at the right for the buttons past the longest option (whose
        // width already counts a submenu's arrow, which is redrawn left of
        // its "?" instead of at the edge).
        menu->setMinimumWidth(menu->sizeHint().width() + OptionHelpButton::SIZE + 18);
        if (arrows)
            menu->setStyleSheet(menu->styleSheet() + QStringLiteral("QMenu::right-arrow { image: none; width: 0px; }"));
        new HelpButtonsPlacer(menu, std::move(entries), dialog_parent);
    }

    std::vector<TopicInfo> allTopics() {
        std::vector<TopicInfo> out;
        for (const Topic& t : topics()) {
            TopicInfo info{ t.id, t.title, {} };
            for (const Block& b : t.blocks) {
                if (!b.first.isEmpty()) info.pictures << b.first;
                if (!b.second.isEmpty()) info.pictures << b.second;
                if (!b.third.isEmpty()) info.pictures << b.third;
            }
            out.push_back(info);
        }
        return out;
    }

} // namespace ui::help
