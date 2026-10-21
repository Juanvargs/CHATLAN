package main

import (
	"database/sql"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unicode/utf8"

	_ "github.com/mattn/go-sqlite3"
)

// User representa una cuenta persistida y su estado de autorización/conexión.
type User struct {
	Code       int    `json:"code"`
	ID         string `json:"id"`
	Public     string `json:"public"`
	Name       string `json:"name"`
	Status     string `json:"status"`
	Online     bool   `json:"online"`
	Disconnect bool   `json:"disconnect_requested"`
	Delete     bool   `json:"delete_requested"`
}

// publicInfo expone solo los datos de usuario que el protocolo permite compartir.
func (u User) publicInfo() packet { return packet{"id": u.ID, "name": u.Name, "code": u.Code} }

// Message es un mensaje guardado antes de distribuirlo a los clientes conectados.
type Message struct {
	ID, Sender, Target, Text, Time, SenderName string
	SenderCode                                 int
}
// Store encapsula la base SQLite y la política de aprobación automática.
type Store struct {
	db          *sql.DB
	autoApprove bool
}

// userCols mantiene el mismo orden de columnas que espera scanUser.
const userCols = "code,id,public,name,status,online,disconnect_requested,delete_requested"

// scanUser convierte una fila SQL en el modelo User.
func scanUser(row interface{ Scan(...any) error }) (User, error) {
	var u User
	// SQLite representa booleanos como 0/1; se reciben primero como enteros.
	var online, disconnect, del int
	// Scan asigna cada columna en el mismo orden declarado por userCols.
	e := row.Scan(&u.Code, &u.ID, &u.Public, &u.Name, &u.Status, &online, &disconnect, &del)
	// Convierte los indicadores numéricos al tipo booleano del modelo Go.
	u.Online = online != 0
	u.Disconnect = disconnect != 0
	u.Delete = del != 0
	return u, e
}
// openStore prepara SQLite, crea tablas si faltan y aplica permisos al archivo de datos.
func openStore(path string, auto bool) (*Store, error) {
	if e := privateDir(filepath.Dir(path)); e != nil {
		return nil, e
	}
	db, e := sql.Open("sqlite3", path+"?_busy_timeout=5000")
	if e != nil {
		return nil, e
	}
	// Una sola conexión ordena escrituras para esta base SQLite local.
	db.SetMaxOpenConns(1)
	// Crea tablas si aún no existen, sin borrar registros al reiniciar.
	schema := []string{
		`CREATE TABLE IF NOT EXISTS clients (code INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,public TEXT UNIQUE NOT NULL,name TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'pending')`,
		`CREATE TABLE IF NOT EXISTS messages (seq INTEGER PRIMARY KEY AUTOINCREMENT,message_id TEXT NOT NULL,sender TEXT NOT NULL,target TEXT NOT NULL,text TEXT NOT NULL,time TEXT NOT NULL)`,
		`CREATE TABLE IF NOT EXISTS deleted_clients (public TEXT PRIMARY KEY,id TEXT NOT NULL,code INTEGER NOT NULL,name TEXT NOT NULL,deleted_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)`,
	}
	for _, q := range schema {
		// Ejecuta cada definición de tabla.
		if _, e = db.Exec(q); e != nil {
			db.Close()
			return nil, e
		}
	}
	// Migra bases anteriores agregando columnas nuevas sin perder sus filas.
	for col, decl := range map[string]string{"online": "INTEGER NOT NULL DEFAULT 0", "last_seen": "TEXT", "disconnect_requested": "INTEGER NOT NULL DEFAULT 0", "delete_requested": "INTEGER NOT NULL DEFAULT 0"} {
		var count int
		rows, e := db.Query("PRAGMA table_info(clients)")
		if e != nil {
			return nil, e
		}
		for rows.Next() {
			// PRAGMA table_info enumera las columnas existentes en clients.
			var cid, notnull, pk int
			var name, typ string
			var def sql.NullString
			if e = rows.Scan(&cid, &name, &typ, &notnull, &def, &pk); e != nil {
				break
			}
			if name == col {
				count++
			}
		}
		rows.Close()
		if e != nil {
			return nil, e
		}
		if count == 0 {
			// ALTER TABLE se ejecuta únicamente si esa columna todavía no existe.
			if _, e = db.Exec("ALTER TABLE clients ADD COLUMN " + col + " " + decl); e != nil {
				return nil, e
			}
		}
	}
	_ = os.Chmod(path, 0600)
	return &Store{db: db, autoApprove: auto}, nil
}
// close libera la conexión SQLite.
func (s *Store) close() error { return s.db.Close() }
// get busca una identidad por su UUID interno.
func (s *Store) get(id string) (User, error) {
	return scanUser(s.db.QueryRow("SELECT "+userCols+" FROM clients WHERE id=?", id))
}
// list devuelve las cuentas ordenadas por el código visible asignado.
func (s *Store) list() ([]User, error) {
	rows, e := s.db.Query("SELECT " + userCols + " FROM clients ORDER BY code")
	if e != nil {
		return nil, e
	}
	defer rows.Close()
	out := []User{}
	for rows.Next() {
		u, e := scanUser(rows)
		if e != nil {
			return nil, e
		}
		out = append(out, u)
	}
	return out, rows.Err()
}
// count cuenta las cuentas que tienen un estado determinado dentro de una transacción.
func count(tx *sql.Tx, status string) (int, error) {
	var n int
	e := tx.QueryRow("SELECT count(*) FROM clients WHERE status=?", status).Scan(&n)
	return n, e
}
// register valida la clave/nombre y registra o recupera el estado de una identidad.
func (s *Store) register(public, name string) (User, error) {
	if _, e := unb64(public, 32); e != nil {
		return User{}, e
	}
	name = strings.TrimSpace(name)
	// Limpia espacios externos y limita el nombre antes de iniciar la escritura.
	if n := utf8.RuneCountInString(name); n < 1 || n > 32 {
		return User{}, errors.New("nombre inválido")
	}
	for _, r := range name {
		if r < 32 {
			return User{}, errors.New("nombre inválido")
		}
	}
	tx, e := s.db.Begin()
	if e != nil {
		return User{}, e
	}
	defer tx.Rollback()
	// El rollback diferido cancela cambios si la función sale antes del Commit final.
	var tomb User
	e = tx.QueryRow("SELECT code,id,public,name FROM deleted_clients WHERE public=?", public).Scan(&tomb.Code, &tomb.ID, &tomb.Public, &tomb.Name)
	// Conserva la lápida de una cuenta eliminada para impedir que la misma clave vuelva a entrar.
	if e == nil {
		// Devuelve el estado eliminado y evita registrar otra cuenta con la misma clave.
		tomb.Status = "deleted"
		return tomb, tx.Commit()
	}
	if e != sql.ErrNoRows {
		// Un error SQL es distinto a que no exista una cuenta con esta clave.
		return User{}, e
	}
	u, e := scanUser(tx.QueryRow("SELECT "+userCols+" FROM clients WHERE public=?", public))
	if e != nil && e != sql.ErrNoRows {
		return User{}, e
	}
	approved, e := count(tx, "approved")
	if e != nil {
		return User{}, e
	}
	if u.ID != "" {
		// La clave ya existe: se conserva su ID y estado en lugar de crear una cuenta duplicada.
		if s.autoApprove && u.Status == "pending" && approved < 10 {
			_, e = tx.Exec("UPDATE clients SET status='approved' WHERE id=?", u.ID)
			if e != nil {
				return User{}, e
			}
			u.Status = "approved"
		}
		return u, tx.Commit()
	}
	pending, e := count(tx, "pending")
	if e != nil {
		return User{}, e
	}
	if pending >= 30 {
		// Rechaza solicitudes nuevas cuando la cola pendiente alcanzó su límite.
		return User{}, errors.New("demasiadas solicitudes pendientes")
	}
	status := "pending"
	// Solo aprueba automáticamente si está activado y todavía hay menos de diez aprobadas.
	if s.autoApprove && approved < 10 {
		status = "approved"
	}
	// Si no se cumplen ambas condiciones, la cuenta se crea pendiente de revisión.
	id, e := newUUID()
	if e != nil {
		return User{}, e
	}
	_, e = tx.Exec("INSERT INTO clients(id,public,name,status) VALUES(?,?,?,?)", id, public, name, status)
	if e != nil {
		return User{}, e
	}
	u, e = scanUser(tx.QueryRow("SELECT "+userCols+" FROM clients WHERE id=?", id))
	if e != nil {
		return User{}, e
	}
	return u, tx.Commit()
}
// setOnline actualiza presencia persistida al conectar o desconectar una cuenta.
func (s *Store) setOnline(id string, online bool) error {
	n := 0
	if online {
		n = 1
	}
	_, e := s.db.Exec("UPDATE clients SET online=?,last_seen=datetime('now'),disconnect_requested=0 WHERE id=?", n, id)
	return e
}
// resetOnline limpia presencia antigua cuando el proceso servidor inicia de nuevo.
func (s *Store) resetOnline() error {
	tx, e := s.db.Begin()
	if e != nil {
		return e
	}
	defer tx.Rollback()
	rows, e := tx.Query("SELECT " + userCols + " FROM clients WHERE delete_requested=1")
	if e != nil {
		return e
	}
	var pending []User
	for rows.Next() {
		u, scanErr := scanUser(rows)
		if scanErr != nil {
			rows.Close()
			return scanErr
		}
		pending = append(pending, u)
	}
	e = rows.Err()
	rows.Close()
	if e != nil {
		return e
	}
	for _, u := range pending {
		if e := deleteRow(tx, u); e != nil {
			return e
		}
	}
	if _, e := tx.Exec("UPDATE clients SET online=0,disconnect_requested=0"); e != nil {
		return e
	}
	return tx.Commit()
}
// setStatus aprueba o revoca una cuenta y comprueba el límite de cuentas aprobadas.
func (s *Store) setStatus(id, status string) error {
	if status != "approved" && status != "revoked" {
		return errors.New("estado inválido")
	}
	tx, e := s.db.Begin()
	if e != nil {
		return e
	}
	defer tx.Rollback()
	u, e := scanUser(tx.QueryRow("SELECT "+userCols+" FROM clients WHERE id=?", id))
	if e != nil {
		return e
	}
	if status == "approved" && u.Status != "approved" {
		// Cuenta de nuevo bajo transacción para no superar el máximo de autorizados.
		n, e := count(tx, "approved")
		if e != nil {
			return e
		}
		if n >= 10 {
			return errors.New("ya hay diez clientes autorizados")
		}
	}
	_, e = tx.Exec("UPDATE clients SET status=? WHERE id=?", status, id)
	if e != nil {
		return e
	}
	return tx.Commit()
}
// disconnect marca una sesión para que el supervisor del servidor la cierre.
func (s *Store) disconnect(id string) error {
	r, e := s.db.Exec("UPDATE clients SET disconnect_requested=1 WHERE id=?", id)
	if e != nil {
		return e
	}
	n, _ := r.RowsAffected()
	if n == 0 {
		return errors.New("cliente inexistente")
	}
	return nil
}
// clearDisconnect limpia la solicitud temporal de desconexión.
func (s *Store) clearDisconnect(id string) error {
	_, e := s.db.Exec("UPDATE clients SET disconnect_requested=0 WHERE id=?", id)
	return e
}
// deleteRow elimina mensajes asociados y conserva una lápida para impedir reutilizar la identidad.
func deleteRow(tx *sql.Tx, u User) error {
	if _, e := tx.Exec("INSERT OR REPLACE INTO deleted_clients(public,id,code,name) VALUES(?,?,?,?)", u.Public, u.ID, u.Code, u.Name); e != nil {
		return e
	}
	if _, e := tx.Exec("DELETE FROM messages WHERE sender=? OR target=?", u.ID, u.ID); e != nil {
		return e
	}
	_, e := tx.Exec("DELETE FROM clients WHERE id=?", u.ID)
	return e
}
// delete borra una cuenta dentro de una transacción y registra su estado eliminado.
func (s *Store) delete(id string, final bool) error {
	tx, e := s.db.Begin()
	if e != nil {
		return e
	}
	defer tx.Rollback()
	u, e := scanUser(tx.QueryRow("SELECT "+userCols+" FROM clients WHERE id=?", id))
	if e == sql.ErrNoRows && final {
		return nil
	}
	if e != nil {
		return e
	}
	if u.Online && !final {
		// Una sesión activa se borra al cerrar; el monitor procesa esta marca.
		_, e = tx.Exec("UPDATE clients SET delete_requested=1 WHERE id=?", id)
	} else {
		// Una cuenta desconectada se elimina en esta misma transacción.
		e = deleteRow(tx, u)
	}
	if e != nil {
		return e
	}
	return tx.Commit()
}
// recover reemplaza la clave pública de una cuenta pendiente usando una identidad nueva.
func (s *Store) recover(id, public string) error {
	if _, e := unb64(public, 32); e != nil {
		return e
	}
	tx, e := s.db.Begin()
	if e != nil {
		return e
	}
	defer tx.Rollback()
	var old string
	if e = tx.QueryRow("SELECT public FROM clients WHERE id=?", id).Scan(&old); e != nil {
		return e
	}
	var otherID, status string
	e = tx.QueryRow("SELECT id,status FROM clients WHERE public=? AND id<>?", public, id).Scan(&otherID, &status)
	if e != nil && e != sql.ErrNoRows {
		return e
	}
	if otherID != "" {
		// Solo se puede liberar una clave asociada a una solicitud que sigue pendiente.
		if status != "pending" {
			return errors.New("la clave pertenece a otra identidad")
		}
		if _, e = tx.Exec("DELETE FROM clients WHERE id=?", otherID); e != nil {
			return e
		}
	}
	_, e = tx.Exec("UPDATE clients SET public=? WHERE id=?", public, id)
	if e != nil {
		return e
	}
	return tx.Commit()
}
// saveMessage persiste el contenido y sus metadatos antes de anunciar aceptación.
func (s *Store) saveMessage(m Message) error {
	_, e := s.db.Exec("INSERT INTO messages(message_id,sender,target,text,time) VALUES(?,?,?,?,?)", m.ID, m.Sender, m.Target, m.Text, m.Time)
	return e
}
// history obtiene hasta 200 mensajes generales o vinculados al usuario.
func (s *Store) history(id string) ([]Message, error) {
	rows, e := s.db.Query(`SELECT m.message_id,m.sender,m.target,m.text,m.time,c.code,c.name FROM messages m JOIN clients c ON c.id=m.sender WHERE m.target='general' OR m.sender=? OR m.target=? ORDER BY m.seq DESC LIMIT 200`, id, id)
	if e != nil {
		return nil, e
	}
	defer rows.Close()
	out := []Message{}
	for rows.Next() {
		var m Message
		if e = rows.Scan(&m.ID, &m.Sender, &m.Target, &m.Text, &m.Time, &m.SenderCode, &m.SenderName); e != nil {
			return nil, e
		}
		out = append(out, m)
	}
	for i, j := 0, len(out)-1; i < j; i, j = i+1, j-1 {
		// La consulta sale de más nuevo a más antiguo; invierte el orden para la vista.
		out[i], out[j] = out[j], out[i]
	}
	return out, rows.Err()
}
// backup crea una copia SQLite coherente en la ruta indicada.
func (s *Store) backup(path string) error {
	if _, e := os.Stat(path); e == nil {
		return errors.New("la copia ya existe")
	}
	safe := strings.ReplaceAll(path, "'", "''")
	_, e := s.db.Exec(fmt.Sprintf("VACUUM INTO '%s'", safe))
	return e
}
